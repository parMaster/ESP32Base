/*
Basic MQTT logger to psql

Next Steps:
	- decouple it nicely
	- add txt writer
	- use containerized Postgres
	- use contenerized Mongo
	- add Mongo Writer
	- cli commands (and mqtt-hook) to replay parts of the raw data back into Queue (from, to, topic)
	-       (all at once, synced, fast forward)
	so I can make a button "replay last hour/day" in IoT OnOff
	
	- save the secrets somewhere

	- try to use a free HiveMQ queue

mosquitto_sub -h .s1.eu.hivemq.cloud -p 8883 -u  -P  -t my/test/topic -d
mosquitto_pub -h .s1.eu.hivemq.cloud -p 8883 -u  -P  -t 'my/test/topic' -m 'Hello'
*/
package main

import (
	"fmt"
	"os"
	"time"
	"database/sql"

	MQTT "github.com/eclipse/paho.mqtt.golang"
	_ "github.com/lib/pq"
)

const (
	host			= "localhost"
	user			= ""
	password		= ""
	dbname			= ""
	mqttUser		= ""
	mqttPassword	= ""
	mqttClientId	= "mqttMonitor"
	mqttBrokerUrl	= "ssl://mqtt.myserver:8883"
)

/*
CREATE TABLE rawdata (
    id bigserial not null primary key,
    dt TIMESTAMPTZ not null,
    topic VARCHAR not null,
    message varchar not null
);
*/

// var logf *os.File

type message struct {
	db *sql.DB
	id int
}

func (m *message) SaveInDB(topic string, message []byte) error {
	return m.db.QueryRow(
		"INSERT INTO rawdata (dt, topic, message) VALUES ($1, $2, $3) RETURNING id",
		time.Now().Format("2006.01.02 15:04:05"),
		topic,
		string(message),
	).Scan(&m.id)
}

// func (m *message) SaveInFile(topic string, message []byte) error {
// var err error
// logf, err = os.Create("./log.log")
// check(err)
// defer logf.Close()
// 	logf.WriteString(fmt.Sprintf("%s | %s \r\n", msg.Topic(), msg.Payload()))
// 	logf.Sync()
// 	fmt.Printf("%s | %s \r\n", msg.Topic(), msg.Payload())
// }

// Save to DB by default
func (m *message) Handle(client MQTT.Client, msg MQTT.Message) {
	m.SaveInDB(msg.Topic(), msg.Payload());
	fmt.Printf("%d\t%s\t%s \r\n", m.id, msg.Topic(), msg.Payload())
}
  
func main() {
	//create a ClientOptions struct setting the broker address, clientid, turn
	//off trace output and set the default message handler
	opts := MQTT.NewClientOptions().AddBroker(mqttBrokerUrl)
	opts.SetUsername(mqttUser)
	opts.SetPassword(mqttPassword)
	opts.SetClientID(mqttClientId)

	// privateKey:	/etc/letsencrypt/live/mqtt.example.com/privkey.pem
	// certificate:	/etc/letsencrypt/live/mqtt.example.com/cert.pem
	// ca:			/etc/letsencrypt/live/mqtt.example.com/chain.pem

	psqlInfo := fmt.Sprintf(
		"host=%s user=%s password=%s dbname=%s sslmode=disable", 
		host, user, password, dbname)
	db, err := sql.Open("postgres", psqlInfo)
	if err != nil {
		panic(err)
	}
	defer db.Close()

	var m = message{db, 0}
	
	opts.SetDefaultPublishHandler(m.Handle)

	//create and start a client using the above ClientOptions
	c := MQTT.NewClient(opts)
	if token := c.Connect(); token.Wait() && token.Error() != nil {
		panic(token.Error())
	}

	// subscribe to every topic (#) and request messages to be delivered
	// at a maximum qos of zero, wait for the receipt to confirm the subscription
	if token := c.Subscribe("#", 1, nil); token.Wait() && token.Error() != nil {
		fmt.Println(token.Error())
		os.Exit(1)
	}

	defer c.Disconnect(500)

	done := make(chan bool, 1)
	<-done
}
