## TLS setup memo

Steps 3-4 from this [manual](https://upcloud.com/community/tutorials/install-secure-mqtt-broker-ubuntu/), saved just the commands, for later use:

### Install certbot

```
add-apt-repository ppa:certbot/certbot
apt update
apt install certbot
ufw allow 80
```

### Make certs (stop http server first)
```
certbot certonly --standalone --preferred-challenges http -d mqtt.example.com
ls /etc/letsencrypt/live/mqtt.example.com
```

### Install Mosquitto MQTT broker

```
apt-add-repository ppa:mosquitto-dev/mosquitto-ppa
apt update
apt install mosquitto mosquitto-clients
mosquitto_passwd -c /etc/mosquitto/passwd _username_
```

### Make custom config (disable anonymous access, enable TLS)

```
vi /etc/mosquitto/conf.d/custom.conf

allow_anonymous false
password_file /etc/mosquitto/passwd
listener 1883 localhost
listener 8883
certfile /etc/letsencrypt/live/mqtt.example.com/cert.pem
cafile /etc/letsencrypt/live/mqtt.example.com/chain.pem
keyfile /etc/letsencrypt/live/mqtt.example.com/privkey.pem
listener 8083
protocol websockets
certfile /etc/letsencrypt/live/mqtt.example.com/cert.pem
cafile /etc/letsencrypt/live/mqtt.example.com/chain.pem
keyfile /etc/letsencrypt/live/mqtt.example.com/privkey.pem
```

### Make certs accessible for mosquitto
```
setfacl -R -m u:mosquitto:rX /etc/letsencrypt/{live,archive}
systemctl restart mosquitto
ufw allow 8883
ufw allow 8083
```

### Test
```
mosquitto_sub -h localhost -t mqtt_topic_name -u "_username_" -P "password"
mosquitto_pub -h mqtt.example.com -t mqtt_topic_name -m "Hello MQTT World" -p 8883 --capath /etc/ssl/certs/ -u "_username_" -P "password"
```





