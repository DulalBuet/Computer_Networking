import paho.mqtt.client as mqtt

broker = "broker.hivemq.com"
topic = "buet/cse/2105169/led" 

client = mqtt.Client()
client.connect(broker, 1883, 60)

print("Press 'y' → LED ON")
print("Press 'n' → LED OFF")
print("Press 'q' → Quit")

while True:
    user_input = input("Enter command: ").lower()

    if user_input == 'y':
        client.publish(topic, "ON")
        print("Sent: ON")

    elif user_input == 'n':
        client.publish(topic, "OFF")
        print("Sent: OFF")

    elif user_input == 'q':
        print("Exiting program...")
        break

    else:
        print("Invalid input! Use y/n/q")

client.disconnect()