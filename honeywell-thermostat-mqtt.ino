// Firmware to turn a rotary thermostat smarter
//
// https://hackaday.io/project/170693-old-thermostat-learns-new-tricks


// Encoder.h is not compatible with the interrupts on the
// ESP chips. Apparently, polling the pins is good enough anyway.
#define ENCODER_DO_NOT_USE_INTERRUPTS

#include <Encoder.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Hardware GPIO pins
#define ENCODER_A 4
#define ENCODER_B 5
#define LED 2

// Too small values make the thermostat loose encoder steps
#define ENCODER_DELAY 10

#define WIFI_SSID "YourWifi"
#define WIFI_PASSWORD "ThePassword"

#define MQTT_SERVER "192.168.82.10"
#define MQTT_USER "guest"
#define MQTT_PASSWORD "guest"

// Event to dispatch when knob changes
#define MQTT_TOPIC_CHANGED "home.thermostat/evt.changed"

// Command to subscribe to change our set temperature
#define MQTT_TOPIC_SET "home.thermostat/cmd.set"

char message_buff[100];

int encoder_states_by_phase[] = {0b00, 0b01, 0b11, 0b10};
int encoder_phases_by_state[] = {0, 1, 3, 2};

long setPoint = 18;

bool debug = true;

WiFiClient espClient;

PubSubClient client(espClient);

Encoder knob(ENCODER_B, ENCODER_A);

void setup()
{
    Serial.begin(9600);
    pinMode(LED, OUTPUT);
    setup_wifi();
    client.setServer(MQTT_SERVER, 1883);
    client.setCallback(callback);
}

void setup_wifi()
{
    delay(10);
    Serial.println();
    Serial.print("Connecting to '");
    Serial.print(WIFI_SSID);
    Serial.print("' .");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("OK");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void reconnect()
{
    while (!client.connected()) {
        Serial.print("Connecting to MQTT broker...");
        if (client.connect("wall.thermostat", MQTT_USER, MQTT_PASSWORD)) {
            Serial.println("OK");
            client.subscribe(MQTT_TOPIC_SET);
        }
        else {
            Serial.print("FAILED: ");
            Serial.print(client.state());
            Serial.println(" Retrying...");
            delay(1000);
        }
    }
}

void loop()
{
    if (!client.connected()) {
        reconnect();
    }
    client.loop();

    long newSetPoint = knob.read();

    if (setPoint != newSetPoint) {

        if (newSetPoint < 0) {
            newSetPoint = 0;
            knob.write(newSetPoint);
        }
        else if (newSetPoint > 60) {
            newSetPoint = 60;
            knob.write(newSetPoint);
        }

        setPoint = newSetPoint;

        if (debug) {
            Serial.print("> ");
            Serial.println(newSetPoint);
        }

        digitalWrite(LED, LOW);
        client.publish(MQTT_TOPIC_CHANGED, String(newSetPoint).c_str(), true);
        digitalWrite(LED, HIGH);
    }
}

void setEncoderPhase(int phase)
{
    int new_state = encoder_states_by_phase[phase];

    digitalWrite(ENCODER_A, (new_state >> 1) & 1);
    digitalWrite(ENCODER_B, new_state & 1);
}

int getEncoderPhase()
{
    int state = (digitalRead(ENCODER_A) << 1) | digitalRead(ENCODER_B);

    return encoder_phases_by_state[state];
}

void callback(char *topic, byte *payload, unsigned int length)
{
    int i = 0;
    for (i = 0; i < length; i++) {
        message_buff[i] = payload[i];
    }
    message_buff[i] = '\0';
    String msgString = String(message_buff);

    if (debug) {
        Serial.println("msg> " + msgString);
    }

    long requestedSetPoint = atol(msgString.c_str());

    // Set the encoder pins to OUTPUT, preserving state
    int phase = getEncoderPhase();
    pinMode(ENCODER_B, OUTPUT);
    pinMode(ENCODER_A, OUTPUT);
    setEncoderPhase(phase);

    // Move CCW, enough to overflow but leaving the phase in such way that
    // going later to the set point will result in the phase unchanged.
    int resetSteps = 80 + (requestedSetPoint % 4);
    for (i = 0; i < resetSteps; i++) {
        client.loop();
        phase = (phase + 3) % 4;
        setEncoderPhase(phase);
        delay(ENCODER_DELAY);
    };

    // Move CC so to reach the set point, the phase after this will
    // be the same as it was before resetting, matching the state
    // of the real encoder switches.
    for (i = 0; i < requestedSetPoint; i++) {
        client.loop();
        phase = (phase + 1) % 4;
        setEncoderPhase(phase);
        delay(ENCODER_DELAY);
    }

    // Back to input mode. The state at this point will be the same
    // as it was when the pins went to OUTPUT mode.
    pinMode(ENCODER_B, INPUT);
    pinMode(ENCODER_A, INPUT);

    knob.write(requestedSetPoint);
}
