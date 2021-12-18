// Use Mosquitto and PAHO
// mosquitto_sub -h c4c85de7f7654c8a838f3b2f8a10d5d0.s1.eu.hivemq.cloud -p 8883 -u gusto -P owXh7dCvGn -t my/test/topic -d
// mosquitto_pub -h c4c85de7f7654c8a838f3b2f8a10d5d0.s1.eu.hivemq.cloud -p 8883 -u gusto -P owXh7dCvGn -t 'my/test/topic' -m 'Hello'

package main

import (
	"fmt"
	"os"

	MQTT "github.com/eclipse/paho.mqtt.golang"
)

var logf *os.File

func check(e error) {
	if e != nil {
		panic(e)
	}
}

//define a function for the default message handler
var f MQTT.MessageHandler = func(client MQTT.Client, msg MQTT.Message) {
	logf.WriteString(fmt.Sprintf("%s | %s \r\n", msg.Topic(), msg.Payload()))
	logf.Sync()

	fmt.Printf("%s | %s \r\n", msg.Topic(), msg.Payload())
}

func main() {
	//create a ClientOptions struct setting the broker address, clientid, turn
	//off trace output and set the default message handler
	opts := MQTT.NewClientOptions().AddBroker("ssl://mqtt.cdns.com.ua:8883")
	opts.SetUsername("gusto")
	opts.SetPassword("")
	opts.SetClientID("go-simple")

	// privateKey:	/etc/letsencrypt/live/mqtt.example.com/privkey.pem
	// certificate:	/etc/letsencrypt/live/mqtt.example.com/cert.pem
	// ca:			/etc/letsencrypt/live/mqtt.example.com/chain.pem

	// For more granular writes, open a file for writing.
	var err error
	logf, err = os.Create("./log.log")
	check(err)
	defer logf.Close()

	opts.SetDefaultPublishHandler(f)

	//create and start a client using the above ClientOptions
	c := MQTT.NewClient(opts)
	if token := c.Connect(); token.Wait() && token.Error() != nil {
		panic(token.Error())
	}

	//subscribe to the topic /go-mqtt/sample and request messages to be delivered
	//at a maximum qos of zero, wait for the receipt to confirm the subscription
	if token := c.Subscribe("#", 1, nil); token.Wait() && token.Error() != nil {
		fmt.Println(token.Error())
		os.Exit(1)
	}

	//Publish 5 messages to /go-mqtt/sample at qos 1 and wait for the receipt
	//from the server after sending each message
	// for i := 0; i < 5; i++ {
	// 	text := fmt.Sprintf("this is msg #%d!", i)
	// 	token := c.Publish("esp32base", 0, false, text)
	// 	token.Wait()
	// }

	// time.Sleep(30 * time.Second)

	done := make(chan bool, 1)
	<-done

	//unsubscribe from /go-mqtt/sample
	// if token := c.Unsubscribe("#"); token.Wait() && token.Error() != nil {
	// 	fmt.Println(token.Error())
	// 	os.Exit(1)
	// }

	// c.Disconnect(250)
}
