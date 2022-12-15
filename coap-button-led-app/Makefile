.phony: all
all: coap_button_led

CXXFLAGS = -std=c++11 -O2 -D_GNU_SOURCE -DWITH_POSIX
LIBRARIES = -lcoap-1 -lwiringPi

coap_button_led: coap_button_led.cpp
	g++ -o $@ $(CXXFLAGS) $< -I/usr/local/include/coap $(LIBRARIES)

.phony: clean
clean:
	rm -f coap_button_led

