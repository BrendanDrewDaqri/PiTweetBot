all: eink_display

eink_display: display.cpp
	g++ -std=c++11 -Wall -Werror -pedantic -O3 -pipe display.cpp -o eink_display
