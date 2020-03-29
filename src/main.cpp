/**************************************************************************************************/
// Project: tlg-tv-remote
// Description: 
//   Send IR signal to control TV through a telegram bot using a ESP8266 or ESP32 microcontroller.
// Created on: 27 mar. 2020
// Last modified date: 29 mar. 2020
// Version: 1.0.0
/**************************************************************************************************/

/* Libraries */

// Standard C/C++ libraries
#include <string.h>

// Device libraries (Arduino ESP32/ESP8266 Cores)
#include <Arduino.h>
#ifdef ESP8266
    #include <ESP8266WiFi.h>
#else
    #include <WiFi.h>
#endif

// Custom libraries
#ifdef ESP8266
    #include <IRremoteESP8266.h>
    #include <IRsend.h>
#elif ESP32
    #include <IRremote.h>
#endif
#include "lg32ls570s.h"
#include <utlgbotlib.h>

/**************************************************************************************************/

// GPIO Pins
#ifdef ESP32
    #define PIN_O_IR_TX 21
    #define PIN_O_STATUS_LED 13
#elif ESP8266
    #define PIN_O_IR_TX 5 // Nodemcu D1
    #define PIN_O_STATUS_LED LED_BUILTIN
#endif

// WiFi Parameters
#define WIFI_SSID "mynet1234"
#define WIFI_PASS "password1234"
#define MAX_CONN_FAIL 50
#define MAX_LENGTH_WIFI_SSID 31
#define MAX_LENGTH_WIFI_PASS 63

// Telegram Bot Token (Get from Botfather)
#define TLG_TOKEN "XXXXXXXXX:XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

// Enable Bot debug level (0 - None; 1 - Bot Level; 2 - Bot+HTTPS Level)
#define DEBUG_LEVEL_UTLGBOT 0

// Telegram Bot /start text message
const char TEXT_START[] = 
    "Hello, im a Bot running in an ESP microcontroller that let you control a TV.\n"
    "\n"
    "Check /help command to see a list of available commands.";

// Telegram Bot /help text message
const char TEXT_HELP[] = 
    "Available Commands:\n"
    "\n"
    "/start - Show start text.\n"
    "/help - Show actual text.\n"
    "/send [CODE] - Send an IR signal of provided NEC.\n";

const char TEXT_SEND_NO_ARG[] = 
    "You need to provide a NEC code.\n"
    "Example:\n"
    "/send 0x10EF";

const char TEXT_SEND_ARG_NO_NUM[] = 
    "Inavlid NEC code.\n"
    "Example:\n"
    "/send 0x10EF";

// Reply Keyboard Markup
const char keyboard[] = 
    "["
        "[\"Power\",\"Prog+\",\"Prog-\"],"
        "[\"7\",\"8\",\"9\",\"Mute\"],"
        "[\"4\",\"5\",\"6\",\"Vol+\"],"
        "[\"1\",\"2\",\"3\",\"Vol-\"],"
        "[\"Media\",\"Up\",\"Info\"],"
        "[\"Left\",\"Ok\",\"Right\"],"
        "[\"Settings\",\"Down\",\"Back\"],"
        "[\"Exit\"]"
    "]";

/**************************************************************************************************/

/* Functions Prototypes */

void wifi_init_stat(void);
bool wifi_handle_connection(void);
void ir_send_nec(const uint16_t code);
uint32_t cstr_count_words(const char* str_in, const size_t str_in_len);
int8_t cstr_string_to_u8(char* str_in, size_t str_in_len, uint8_t* value_out, uint8_t base);
int8_t cstr_string_to_u16(char* str_in, size_t str_in_len, uint16_t* value_out, uint8_t base);
int8_t cstr_string_to_u32(char* str_in, size_t str_in_len, uint32_t* value_out, uint8_t base);

/**************************************************************************************************/

/* Data Types */

// Functions Return Codes
enum _return_codes
{
    RC_OK = 0,
    RC_BAD = -1,
    RC_INVALID_INPUT = -2,
    RC_CUSTOM_DELAY = 100
};

/**************************************************************************************************/

/* Globals */

// Create Bot object
uTLGBot Bot(TLG_TOKEN);

// IR Send object
IRsend IRtx(PIN_O_IR_TX);

/**************************************************************************************************/

/* Main Function */

void setup(void)
{
    // Enable Bot debug
    Bot.set_debug(DEBUG_LEVEL_UTLGBOT);

    // Initialize Serial
    Serial.begin(115200);

    // Initialize LED
    digitalWrite(PIN_O_STATUS_LED, HIGH);
    pinMode(PIN_O_STATUS_LED, OUTPUT);

    // Initialize IR
    #ifdef ESP8266
        IRtx.begin();
    #endif

    // Initialize WiFi station connection
    wifi_init_stat();

    // Wait for WiFi connection
    Serial.println("Waiting for WiFi connection.");
    while(!wifi_handle_connection())
    {
        Serial.print(".");
        delay(500);
    }

    // Get Bot account info
    Bot.getMe();
}

void loop()
{
    // Check if WiFi is connected
    if(!wifi_handle_connection())
    {
        // Wait 100ms and check again
        delay(100);
        return;
    }

    // Check for Bot received messages
    if(Bot.getUpdates())
    {
        // Turn on the LED
        digitalWrite(PIN_O_STATUS_LED, LOW);

        // Show received message text
        Serial.println("");
        Serial.println("Received message:");
        Serial.println(Bot.received_msg.text);
        Serial.println("");

        // If /start command was received, send the keyboard
        if(strncmp(Bot.received_msg.text, "/start\0", strlen("/start\0")) == 0)
            Bot.sendReplyKeyboardMarkup(Bot.received_msg.chat.id, TEXT_START, keyboard);

        // If /help command was received, send the help message
        else if(strncmp(Bot.received_msg.text, "/help\0", strlen("/help\0")) == 0)
            Bot.sendMessage(Bot.received_msg.chat.id, TEXT_HELP);

        // If /send command was received, check and send a custom IR NEC signal
        else if(strncmp(Bot.received_msg.text, "/send", strlen("/send")) == 0)
        {
            uint16_t nec_code = 0x0000;
            char* ptr_cmd = Bot.received_msg.text;
            char* ptr_argv;

            // If an argument has been provided
            if(cstr_count_words(Bot.received_msg.text, strlen(Bot.received_msg.text)) < 2)
                Bot.sendMessage(Bot.received_msg.chat.id, TEXT_SEND_NO_ARG);

            // Point to second command argument
            while(1)
            {
                ptr_argv = strstr(ptr_cmd, " ");
                if(ptr_argv == NULL)
                {
                    Bot.sendMessage(Bot.received_msg.chat.id, TEXT_SEND_NO_ARG);
                    break;
                }
                if((uint16_t)(ptr_argv - ptr_cmd) >= strlen(ptr_cmd))
                {
                    Bot.sendMessage(Bot.received_msg.chat.id, TEXT_SEND_NO_ARG);
                    break;
                }
                ptr_argv = ptr_argv + 1;

                // Convert argument into integer type
                if(cstr_string_to_u16(ptr_argv, strlen(ptr_argv), &nec_code, 16) != RC_OK)
                {
                    Bot.sendMessage(Bot.received_msg.chat.id, TEXT_SEND_ARG_NO_NUM);
                    break;
                }

                // Send the signal
                char msg[36];
                ir_send_nec(nec_code);
                snprintf(msg, 36, "IR Custom signal (0x%04X) sent.", nec_code);
                Bot.sendMessage(Bot.received_msg.chat.id, msg);

                break;
            }
        }

        // If keyboard key command was received
        else if(strncmp(Bot.received_msg.text, "Power\0", strlen("Power\0")) == 0)
        {
            ir_send_nec(LG_POWER);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Power signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Vol+\0", strlen("Vol+\0")) == 0)
        {
            ir_send_nec(LG_VOL_PLUS);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Vol+ signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Vol-\0", strlen("Vol-\0")) == 0)
        {
            ir_send_nec(LG_VOL_LESS);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Vol- signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Prog+\0", strlen("Prog+\0")) == 0)
        {
            ir_send_nec(LG_PROG_PLUS);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Prog+ signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Prog-\0", strlen("Prog-\0")) == 0)
        {
            ir_send_nec(LG_PROG_LESS);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Prog- signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Mute", strlen("Mute\0")) == 0)
        {
            ir_send_nec(LG_MUTE);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Mute signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "1\0", strlen("1\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_1);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 1 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "2\0", strlen("2\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_2);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 2 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "3\0", strlen("3\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_3);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 3 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "4\0", strlen("4\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_4);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 4 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "5\0", strlen("5\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_5);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 5 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "6\0", strlen("6\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_6);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 6 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "7\0", strlen("7\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_7);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 7 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "8\0", strlen("8\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_8);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 8 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "9\0", strlen("9\0")) == 0)
        {
            ir_send_nec(LG_NUMBER_9);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR num 9 signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Up\0", strlen("Up\0")) == 0)
        {
            ir_send_nec(LG_UP);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Up signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Right\0", strlen("Right\0")) == 0)
        {
            ir_send_nec(LG_RIGHT);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Right signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Left\0", strlen("Left\0")) == 0)
        {
            ir_send_nec(LG_LEFT);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Left signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Down\0", strlen("Down\0")) == 0)
        {
            ir_send_nec(LG_DOWN);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Down signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "OK\0", strlen("OK\0")) == 0)
        {
            ir_send_nec(LG_OK);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR OK signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Back\0", strlen("Back\0")) == 0)
        {
            ir_send_nec(LG_BACK);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Back signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Exit\0", strlen("Exit\0")) == 0)
        {
            ir_send_nec(LG_EXIT);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Down signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Media\0", strlen("Media\0")) == 0)
        {
            ir_send_nec(LG_INPUT);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Media signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Info\0", strlen("Info\0")) == 0)
        {
            ir_send_nec(LG_INFO);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Info signal sent.");
        }
        else if(strncmp(Bot.received_msg.text, "Settings\0", strlen("Settings\0")) == 0)
        {
            ir_send_nec(LG_SETTINGS);
            Bot.sendMessage(Bot.received_msg.chat.id, "IR Settings signal sent.");
        }
    }

    // Wait 1s for next iteration
    delay(1000);

    // Turn off the LED
    digitalWrite(PIN_O_STATUS_LED, HIGH);
}

/**************************************************************************************************/

/* Functions */

// Init WiFi interface
void wifi_init_stat(void)
{
    Serial.println("Initializing TCP-IP adapter...");
    Serial.print("Wifi connecting to SSID: ");
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    //WiFi.config(staticIP, gateway, subnet, dns);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.println("TCP-IP adapter successfuly initialized.");
}

/**************************************************************************************************/

/* WiFi Change Event Handler */

bool wifi_handle_connection(void)
{
    static bool wifi_connected = false;

    // Device is not connected
    if(WiFi.status() != WL_CONNECTED)
    {
        // Was connected
        if(wifi_connected)
        {
            Serial.println("WiFi disconnected.");
            wifi_connected = false;
        }

        return false;
    }
    // Device connected
    else
    {
        // Wasn't connected
        if(!wifi_connected)
        {
            Serial.println("");
            Serial.println("WiFi connected");
            Serial.print("IP address: ");
            Serial.println(WiFi.localIP());

            wifi_connected = true;
        }

        return true;
    }
}

/**************************************************************************************************/

// Send an IR NEC code message
void ir_send_nec(const uint16_t code)
{
    IRtx.sendNEC(NEC_INIT_MASK | code, 32);
}

/**************************************************************************************************/

/* Auxiliar Functions */

// Count the number of words inside a string
uint32_t cstr_count_words(const char* str_in, const size_t str_in_len)
{
    uint32_t n = 1;

    // Check if string is empty
    if(str_in[0] == '\0')
        return 0;

    // Check for character occurrences
    for(size_t i = 1; i < str_in_len; i++)
    {
        // Check if end of string detected
        if(str_in[i] == '\0')
            break;

        // Check if pattern "X Y", "X\rY" or "X\nY" does not meet
        if((str_in[i] != ' ') && (str_in[i] != '\r') && (str_in[i] != '\n'))
            continue;
        if((str_in[i-1] == ' ') || (str_in[i-1] == '\r') || (str_in[i-1] == '\n'))
            continue;
        if((str_in[i+1] == ' ') || (str_in[i+1] == '\r') || (str_in[i+1] == '\n'))
            continue;
        if(str_in[i+1] == '\0')
            continue;

        // Pattern detected, increase word count
        n = n + 1;
    }

    return n;
}

// Convert string into unsigned 8 bytes value
int8_t cstr_string_to_u8(char* str_in, size_t str_in_len, uint8_t* value_out, uint8_t base)
{
    return cstr_string_to_u32(str_in, str_in_len, (uint32_t*)value_out, base);
}

// Convert string into unsigned 16 bytes value
int8_t cstr_string_to_u16(char* str_in, size_t str_in_len, uint16_t* value_out, uint8_t base)
{
    return cstr_string_to_u32(str_in, str_in_len, (uint32_t*)value_out, base);
}

// Convert string into unsigned 32 bytes value
int8_t cstr_string_to_u32(char* str_in, size_t str_in_len, uint32_t* value_out, uint8_t base)
{
    char* ptr = str_in;
	uint8_t digit;

	*value_out = 0;

    // Check for hexadecimal "0x" bytes and go through it
    if(base == 16)
    {
        if((ptr[1] == 'x') || (ptr[1] == 'X'))
        {
            if(str_in_len < 3)
                return RC_INVALID_INPUT;
            ptr = ptr + 2;
            str_in_len = str_in_len - 2;
        }
    }

    // Convert each byte of the string
    for(uint16_t i = 0; i < str_in_len; i++)
    {
        if(base == 10)
        {
            if(ptr[i] >= '0' && ptr[i] <= '9')
			    digit = ptr[i] - '0';
            else
                return RC_INVALID_INPUT;
        }
        else if(base == 16)
        {
            if(ptr[i] >= '0' && ptr[i] <= '9')
                digit = ptr[i] - '0';
            else if (base == 16 && ptr[i] >= 'a' && ptr[i] <= 'f')
                digit = ptr[i] - 'a' + 10;
            else if (base == 16 && ptr[i] >= 'A' && ptr[i] <= 'F')
                digit = ptr[i] - 'A' + 10;
            else
                return RC_INVALID_INPUT;
        }
        else
			return RC_INVALID_INPUT;

		*value_out = ((*value_out)*base) + digit;
	}

	return RC_OK;
}
