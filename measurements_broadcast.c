#include "contiki.h"
#include "httpd-simple.h"
#include "dev/sht11-sensor.h"
#include "dev/light-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-udp-packet.h"
#include "simple-udp.h"

#define UDP_PORT 1234

PROCESS(web_sense_process, "Sense Web Demo");
PROCESS(webserver_nogui_process, "Web server");

PROCESS_THREAD(webserver_nogui_process, ev, data)
{
        PROCESS_BEGIN();

        httpd_init();

        while(1)
        {
                PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);
                httpd_appcall(data);
        }

        PROCESS_END();
}

AUTOSTART_PROCESSES(&web_sense_process,&webserver_nogui_process);

#define HISTORY 16

static int temperature[HISTORY];
static int light[HISTORY];
static int humidity[HISTORY];
static int solar[HISTORY];

static int min_light = 9999;
static int max_light = 0;
static int avg_light;

static int min_temperature = 9999;
static int max_temperature = 0;
static int avg_temperature;

static int min_humidity = 9999;
static int max_humidity = 0;
static int avg_humidity;

static int min_solar = 9999;
static int max_solar = 0;
static int avg_solar;

static int hits_counter = 0;

static int sensors_pos;


// measurements got from broadcasted messages between nodes
static int received_light = -1;
static int received_temperature = -1;
static int received_humidity = -1;
static int received_solar = -1;

// struct that holds the measurements to be sent and received via broadcast
struct measurements
{
        int light;
        int temperature;
        int humidity;
        int solar;
};

static struct simple_udp_connection broadcast_connection;

static void receiver(struct simple_udp_connection *c, const uip_ipaddr_t *sender_addr, uint16_t sender_port, const uip_ipaddr_t *receiver_addr, uint16_t receiver_port, const uint8_t *data, uint16_t datalen)
{
        struct measurements *meas;
        meas = (struct measurements *)data;

        // initializing/refreshing the broadcasted measurements from another node
        received_light = meas->light;
        received_temperature = meas->temperature;
        received_humidity = meas->humidity;
        received_solar = meas->solar;
}


static int get_light(void)
{
        return 10 * light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC) / 7;
}

static int get_temperature(void)
{
        return ((sht11_sensor.value(SHT11_SENSOR_TEMP) / 10) - 396) / 10;
}

static int get_humidity(void)
{
        return sht11_sensor.value(SHT11_SENSOR_HUMIDITY) / 100;
}

static int get_solar(void)
{
        return sht11_sensor.value(LIGHT_SENSOR_TOTAL_SOLAR) / 100;
}


static const char *TOP = "<html><head><title>Contiki Web Sense</title></head><body>\n";

static const char *BOTTOM = "</body></html>\n";

// Only one single request at time
static char buf[256];
static int blen;
#define ADD(...) do {                                                   \
    blen += snprintf(&buf[blen], sizeof(buf) - blen, __VA_ARGS__);      \
  } while(0)


static PT_THREAD(send_values(struct httpd_state *s))
{
        hits_counter++;

        PSOCK_BEGIN(&s->sout);

        SEND_STRING(&s->sout, TOP);

        blen = 0;

        ADD("Hits: %d<br>"
        "Light: %d lx (min: %d, max: %d, avg: %d)<br>"
        "Temperature: %d&deg;C (min: %d, max: %d, avg: %d)<br>"
        "Humidity: %d%% (min: %d, max: %d, avg: %d)<br>"
        "Solar: %d lx (min: %d, max: %d, avg: %d)<br>",
        hits_counter,
        get_light(), min_light, max_light, avg_light,
        get_temperature(), min_temperature, max_temperature, avg_temperature,
        get_humidity(), min_humidity, max_humidity, avg_humidity,
        get_solar(), min_solar, max_solar, avg_solar);

        SEND_STRING(&s->sout, buf);

        SEND_STRING(&s->sout, BOTTOM);

        PSOCK_END(&s->sout);
}


httpd_simple_script_t httpd_simple_get_script(const char *name)
{
        return send_values;
}


PROCESS_THREAD(web_sense_process, ev, data)
{
        static struct etimer timer;

        PROCESS_BEGIN();

        sensors_pos = 0;

        etimer_set(&timer, CLOCK_SECOND * 2);
        SENSORS_ACTIVATE(light_sensor);
        SENSORS_ACTIVATE(sht11_sensor);

        uip_ipaddr_t addr;
        simple_udp_register(&broadcast_connection, UDP_PORT, NULL, UDP_PORT, receiver);

        while(1)
        {
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timer));
                etimer_reset(&timer);

                // measuring light and getting max, min and average values of it
                //---------------------------------------------------------------------------------------------------
                light[sensors_pos] = get_light();

                if(light[sensors_pos]<min_light)
                        min_light = light[sensors_pos];

                if(light[sensors_pos]>max_light)
                        max_light = light[sensors_pos];

                avg_light = (avg_light + light[sensors_pos])/2;

                // if there hasn't been a broadcast message received yet, don't bother
                if(received_light != -1)
                        avg_light = (avg_light + received_light)/2;
                //---------------------------------------------------------------------------------------------------


                // measuring temperature and getting max, min and average values of it
                //---------------------------------------------------------------------------------------------------
                temperature[sensors_pos] = get_temperature();

                if(temperature[sensors_pos]<min_temperature)
                        min_temperature = temperature[sensors_pos];

                if(temperature[sensors_pos]>max_temperature)
                        max_temperature = temperature[sensors_pos];

                avg_temperature = (avg_temperature + temperature[sensors_pos])/2;

                // if there hasn't been a broadcast message received yet, don't bother
                if(received_temperature != -1)
                        avg_temperature = (avg_temperature + received_temperature)/2;
               //---------------------------------------------------------------------------------------------------


                // measuring humidity and getting max, min and average values of it
                //---------------------------------------------------------------------------------------------------
                humidity[sensors_pos] = get_humidity();

                if(humidity[sensors_pos]<min_humidity)
                        min_humidity = humidity[sensors_pos];

                if(humidity[sensors_pos]>max_humidity)
                        max_humidity = humidity[sensors_pos];

                avg_humidity = (avg_humidity + humidity[sensors_pos])/2;

                // if there hasn't been a broadcast message received yet, don't bother
                if(received_humidity != -1)
                        avg_humidity = (avg_humidity + received_humidity)/2;
                //---------------------------------------------------------------------------------------------------


                // measuring solar light and getting max, min and average values of it
                //---------------------------------------------------------------------------------------------------
                solar[sensors_pos] = get_solar();

                if(solar[sensors_pos]<min_solar)
                        min_solar = solar[sensors_pos];

                if(solar[sensors_pos]>max_solar)
                        max_solar = solar[sensors_pos];

                avg_solar = (avg_solar + solar[sensors_pos])/2;

                // if there hasn't been a broadcast message received yet, don't bother
                if(received_solar != -1)
                        avg_solar = (avg_solar + received_solar)/2;
                //---------------------------------------------------------------------------------------------------

                sensors_pos = (sensors_pos + 1) % HISTORY;


                struct measurements meas;

                meas.light = avg_light;
                meas.temperature = avg_temperature;
                meas.humidity = avg_humidity;
                meas.solar = avg_solar;

                // sending broadcast message to neighbor nodes...
                uip_create_linklocal_allnodes_mcast(&addr);
                simple_udp_sendto(&broadcast_connection, &meas, 20, &addr);
        }

        PROCESS_END();
}
