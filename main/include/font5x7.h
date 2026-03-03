#ifndef FONT5x7_H
#define FONT5x7_H


#include <stdint.h>


#define CHAR_WIDTH      6
#define CHAR_HEIGHT     7


extern const uint8_t font5x7_digits[10][5];   // '0'–'9'
extern const uint8_t font5x7_upper[26][5];    // 'A'–'Z'
extern const uint8_t font5x7_lower[26][5]; 


const uint8_t *get_glyph(char c);


#endif