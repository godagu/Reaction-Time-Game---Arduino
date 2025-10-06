// Author: Goda Gutparakyte
// Assignment: HW2 Intro to Robotics at Vilnius university
// Description: Reaction time game
// Last upfated: 2025 10 07

#include <LiquidCrystal.h>
#include <avr/interrupt.h>
#include <EEPROM.h>

#define LED_GREEN_PIN 12
#define LED_RED_PIN 13
#define BUTTON_PIN 2
#define BUTTON_MENU_PIN 3
#define RS_PIN 11
#define E_PIN 10
#define DB4_PIN 7
#define DB5_PIN 6
#define DB6_PIN 5
#define DB7_PIN 4
#define PIEZO_PIN 9

// EEPROM constants
const uint16_t EEPROM_MAGIC = 0x5254; // 'R' 'T'
const uint8_t EEPROM_VERSION = 1;
const uint16_t EMPTY_SCORE = 0xFFFF;

const int EEPROM_ADDR_MAGIC = 0;
const int EEPROM_ADDR_VERSION = 2;
const int EEPROM_ADDR_COUNT = 3;
const int EEPROM_ADDR_SCORES = 4;

// Top score constants
const uint8_t TOP_SCORE_BEEP_TOTAL = 3;
const uint32_t TOP_SCORE_BEEP_DURATION = 400; // ms
const uint32_t TOP_SCORE_BEEP_INTERVAL = 200; // ms
const uint8_t MAX_SCORES = 10;

// Display constants
const uint32_t SCOREBOARD_PAGE_MS = 1500;
const uint32_t SUCCESS_DISPLAY_MS = 3000;
const uint32_t FAIL_DISPLAY_MS = 2000;

// External interrupts variables
volatile bool button_pressed = false;
volatile bool menu_pressed = false;
volatile uint32_t ms_ticks = 0;
volatile uint32_t last_button_event_time = 0;

// Variables for managing display duration
uint32_t random_end_time = 0;
uint32_t start_time = 0;
uint32_t reaction_time = 0;
uint32_t display_end_time = 0;

// Top score variables
uint16_t top_scores[MAX_SCORES];
uint8_t top_count = 0;
bool is_displaying_scoreboard = false;
uint8_t scoreboard_current_page = 0;
uint32_t scoreboard_last_page_change = 0;


// Top score variables for piezo 
bool top_score_beep_active = false;
uint32_t top_score_beep_next_time = 0;
uint8_t top_score_beep_count = 0;

// ?
LiquidCrystal lcd(RS_PIN, E_PIN, DB4_PIN, DB5_PIN, DB6_PIN, DB7_PIN);

// States
enum State { IDLE, WAIT_RANDOM, READY, DISPLAY_MAIN };
State state = IDLE;
State next_state_after_display = IDLE;

// interrupt for main play button
void button_ISR() {
    uint32_t t = ms_ticks;    // anything under 250 ms disregarded (for debouncing)
    if ((int32_t)(t - last_button_event_time) < 250) {
        return;
    } 

    last_button_event_time = t;
    button_pressed = true;
}

// interrupt for menu button
void menu_ISR() {
    static uint32_t last_event = 0;
    uint32_t t = ms_ticks;
    // debouncing logic
    if ((int32_t)(t - last_event) > 50) {
        last_event = t;
        if (digitalRead(BUTTON_MENU_PIN) == LOW) {
            menu_pressed = true;
        }
    }
}


// EEPROM utilities

// reads a value from an address
uint16_t eeprom_read_uint16(int addr) {
    // eeprom can read/write 1 byte at a time, so read twice
    uint8_t lo = EEPROM.read(addr);
    uint8_t hi = EEPROM.read(addr + 1);
    return (uint16_t)hi << 8 | lo;
}

// writes value to address id it's updated
void eeprom_write_uint16_if_changed(int addr, uint16_t val) {
    uint16_t old = eeprom_read_uint16(addr);
    if (old != val) {
        EEPROM.update(addr, val & 0xFF);
        EEPROM.update(addr + 1, val >> 8);
    }
}

// saves top scores to EEPROM
void save_top_scores_to_EEPROM() {
    // updates magic number if needed
    eeprom_write_uint16_if_changed(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);

    // check and update version if needed
    uint8_t ver = EEPROM.read(EEPROM_ADDR_VERSION);
    if (ver != EEPROM_VERSION) EEPROM.update(EEPROM_ADDR_VERSION, EEPROM_VERSION);

    // checks and updates top score amunt (how much are actually filled)
    if (EEPROM.read(EEPROM_ADDR_COUNT) != top_count)
        EEPROM.update(EEPROM_ADDR_COUNT, top_count);

    // writes the top scores or empty score if slot is not taken
    for (uint8_t i = 0; i < MAX_SCORES; ++i) {
        uint16_t val = (i < top_count) ? top_scores[i] : EMPTY_SCORE;
        eeprom_write_uint16_if_changed(EEPROM_ADDR_SCORES + 2 * i, val);
    }
}

// loads top scores from EEPROM to top_scores variable
void load_top_scores_from_EEPROM() {
    // read magic and version 
    uint16_t magic = eeprom_read_uint16(EEPROM_ADDR_MAGIC);
    uint8_t ver = EEPROM.read(EEPROM_ADDR_VERSION);

    // if one is wrong, eeprom datat is invalid or outdated so resets everything and returns
    if (magic != EEPROM_MAGIC || ver != EEPROM_VERSION) {
        top_count = 0;
        for (uint8_t i = 0; i < MAX_SCORES; ++i) top_scores[i] = EMPTY_SCORE;
        save_top_scores_to_EEPROM();
        return;
    }

    // reads the number of results saved and udpdates top_count
    uint8_t count = EEPROM.read(EEPROM_ADDR_COUNT);
    if (count > MAX_SCORES) count = MAX_SCORES;
    top_count = count;

    // fills top_scores array with scores from EEPRON
    for (uint8_t i = 0; i < MAX_SCORES; ++i)
        top_scores[i] = eeprom_read_uint16(EEPROM_ADDR_SCORES + 2 * i);
}

// adding score (true - score made it to top scores, false - it didnt)
bool add_score(uint16_t ms) {
    // cannot insert empty score
    if (ms == EMPTY_SCORE) return false;

    // checks if score is better than any of the scores listed already
    int insert_at = -1;
    for (int i = 0; i < MAX_SCORES; ++i) {
        uint16_t v = top_scores[i];
        if (v == EMPTY_SCORE || ms < v) {
            insert_at = i;
            break;
        }
    }

    // score did not make it to top scores
    if (insert_at == -1) return false;

    // make space for new score
    for (int i = MAX_SCORES - 1; i > insert_at; --i)
        top_scores[i] = top_scores[i - 1];

    // insert new score and increase score count
    top_scores[insert_at] = ms;
    if (top_count < MAX_SCORES) ++top_count;

    // save everything to eeprom
    save_top_scores_to_EEPROM();
    return true;
}

// LCD scoreboard utilities

// displayes specific index of scores page
void display_scores_page(uint8_t page_index) {
    lcd.clear();

    // each page shows two scores
    uint8_t first = page_index * 2;
    for (uint8_t r = 0; r < 2; ++r) {
        uint8_t idx = first + r;
        lcd.setCursor(0, r);
        if (idx < MAX_SCORES) {
            // max 16 + null terminating (16 x 2 screen)
            char buf[17];
            // print score or empty slot
            if (top_scores[idx] != EMPTY_SCORE) {
                snprintf(buf, sizeof(buf), "%d) %u ms", idx + 1, (unsigned)top_scores[idx]);
            } else {
                snprintf(buf, sizeof(buf), "%d) ---", idx + 1);
            }
            lcd.print(buf);
        }
    }
}

// two results per page
uint8_t scoreboard_page_count() {
    return (MAX_SCORES + 1) / 2;
}

// Timer handling

// timer interrupt
ISR(TIMER1_COMPA_vect) { 
    ++ms_ticks; 
}

// unsafe to return directly ms_ticks (if reading happens and interrupt happens)
uint32_t get_millis() {
    uint32_t t;
    noInterrupts();
    t = ms_ticks;
    interrupts();
    return t;
}

// https://deepbluembedded.com/arduino-timer-interrupts/
void setupTimer1_1ms() {
    noInterrupts();
    // set timer/counter contrpl registers A and B to 0 (both 8 bits)
    TCCR1A = 0;
    TCCR1B = 0;
    // reset the timer counter
    TCNT1 = 0;
    // set the otuput compare register
    // freq = Fcpu / (prescaler * (OCR1A + 1))
    // OCR1A = (Fcpu / (freq * prescaler)) - 1
    // Fcpu = 16000000 (16 MHz Arduino uno)
    // freq = 1000 (1 ms period)
    // prescaler - hardware timer module used to divide clocks signals freq to bring down the timer clock so it tkaes longer for ovverflow
    // f_timer = Fcpu / Prescaler --> Prescaler = Fcpu / Ftimer
    // counts = F_timer x 0.001
    // if prescaler = 64, then F_timer = 250 000 GHz --> Counts = 250 000 / 0.001 = 250 (works good with OCR1A)
    // if bigger - inaccurate, smaller - more counts per interrput, more time for cpu
    // prescaler = 64 (1 ms period)
    // OCR1A = (16000000 / (64*1000)) - 1 = 249
    OCR1A = 249;
    // set CTC (clear timer on compare match) mode
    // each register bit is for specific function
    // WGM12 is 3rd bit
    // CTC - when TCNT1 == OCR1A --> timeris nusinulina
    // timer counts until OCR1A value (1 ms) and generates interrupt
    TCCR1B |= (1 << WGM12);
    // Clock select bits: sets prescaler to 64
    TCCR1B |= (1 << CS11) | (1 << CS10);
    // timer/counter interrupt mask register - tunr on or off timer1 interrupt
    // whenever timer1 reaches OCR1A, call interrupt
    TIMSK1 |= (1 << OCIE1A);
    interrupts();
}


void setup() {
    // debug
    Serial.begin(9600);

    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(BUTTON_MENU_PIN, INPUT_PULLUP);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(PIEZO_PIN, OUTPUT);

    // start screen
    lcd.begin(16, 2);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("REACTION TEST");
    lcd.setCursor(0, 1);
    lcd.print("Press to start");

    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_RED_PIN, HIGH);

    randomSeed(analogRead(A0));
    setupTimer1_1ms();
    load_top_scores_from_EEPROM();

    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_MENU_PIN), menu_ISR, FALLING);
}

void loop() {
    uint32_t now = get_millis();

    // menu button pressed
    if (menu_pressed) {
        menu_pressed = false;

        // only in IDLE state, not while playing
        if (state == IDLE) {
            scoreboard_current_page = 0;
            scoreboard_last_page_change = now;
            uint8_t page_count = scoreboard_page_count();
            uint32_t total_show_ms = (uint32_t)page_count * SCOREBOARD_PAGE_MS;

            is_displaying_scoreboard = true;

            // display scores or no scores
            if (page_count > 0) {
                display_end_time = now + total_show_ms;
                display_scores_page(scoreboard_current_page);
                scoreboard_last_page_change = now;
            } else {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("No Scores Yet");
                display_end_time = now + FAIL_DISPLAY_MS;
            }

            state = DISPLAY_MAIN;
            next_state_after_display = IDLE;

            digitalWrite(LED_GREEN_PIN, LOW);
            digitalWrite(LED_RED_PIN, HIGH);
        }
    }

    // main states
    switch (state) {

        // press to start -> wait
        case IDLE:
            digitalWrite(LED_GREEN_PIN, LOW);
            digitalWrite(LED_RED_PIN, HIGH);

            // random delay for game
            if (button_pressed) {
                button_pressed = false;
                uint32_t delayMs = random(1000, 5000);
                random_end_time = now + delayMs;
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Wait...");
                state = WAIT_RANDOM;
            }
            break;

        // press now! 
        case WAIT_RANDOM:
            // green led lights up
            digitalWrite(LED_GREEN_PIN, LOW);
            digitalWrite(LED_RED_PIN, HIGH);

            // button pressed too early, reset
            if (button_pressed) {
                button_pressed = false;
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("Cheating?:)");
                display_end_time = now + FAIL_DISPLAY_MS;
                is_displaying_scoreboard = false;
                next_state_after_display = IDLE;
                state = DISPLAY_MAIN;
            } else if (now >= random_end_time) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("NOW!");
                start_time = now;
                state = READY;
            }
            break;
        // after game is played
        case READY:
            // reset leds to normal
            digitalWrite(LED_GREEN_PIN, HIGH);
            digitalWrite(LED_RED_PIN, LOW);

            if (button_pressed) {
                button_pressed = false;
                reaction_time = now - start_time;
               
                lcd.clear();
                lcd.setCursor(0, 1);
                lcd.print(reaction_time);
                lcd.print(" ms");

                // if score makes it to top scores
                if (add_score((uint16_t)reaction_time)) {
                    lcd.setCursor(0, 0);
                    lcd.print("TOP ");
                    lcd.print(MAX_SCORES);
                    lcd.print(" SCORE!");
                    top_score_beep_active = true;
                    top_score_beep_next_time = now;
                    top_score_beep_count = 0;
                }
                else{
                    lcd.setCursor(0, 0);
                    lcd.print("Too slow...");

                }

                // reset
                display_end_time = now + SUCCESS_DISPLAY_MS;
                is_displaying_scoreboard = false;
                next_state_after_display = IDLE;
                state = DISPLAY_MAIN;
            }
            break;

        case DISPLAY_MAIN:
            digitalWrite(LED_GREEN_PIN, LOW);
            digitalWrite(LED_RED_PIN, HIGH);

            // display scoreboard
            if (is_displaying_scoreboard && (display_end_time > now)) {
                if (scoreboard_page_count() > 0) {
                    if ((int32_t)(now - scoreboard_last_page_change) >= (int32_t)SCOREBOARD_PAGE_MS) {
                        if (scoreboard_current_page < scoreboard_page_count() - 1) {
                            ++scoreboard_current_page;
                            display_scores_page(scoreboard_current_page);
                            scoreboard_last_page_change = now;
                        }
                    }
                }
            }

            // reset to start screen
            if ((int32_t)(now - display_end_time) >= 0) {
                lcd.clear();
                lcd.setCursor(0, 0);
                lcd.print("REACTION TEST");
                lcd.setCursor(0, 1);
                lcd.print("Press to start");
                state = IDLE;
                button_pressed = false;
                scoreboard_current_page = 0;
                is_displaying_scoreboard = false;
                next_state_after_display = IDLE;
            }
            break;

        default:
            break;
    }

    // beeping if top score achieved
    if (top_score_beep_active) {
        if ((int32_t)(now - top_score_beep_next_time) >= 0) {
            if (top_score_beep_count < TOP_SCORE_BEEP_TOTAL) {
                tone(PIEZO_PIN, 1000 + top_score_beep_count * 200, TOP_SCORE_BEEP_DURATION);
                top_score_beep_next_time = now + TOP_SCORE_BEEP_INTERVAL;
                ++top_score_beep_count;
            } else {
                noTone(PIEZO_PIN);
                top_score_beep_active = false;
            }
        }
    }
}
