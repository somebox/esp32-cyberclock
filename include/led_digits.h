/*
    Functions for controlling the 7-segment digits via shift registers.
*/


int reverse(int n){
  int rev = 0;
  int i = DISPLAY_SIZE;
  while (i>0){
    int rem = n % 10;
    rev = rev*10 + rem;
    n /= 10;
    i--;
  }
  return rev;
}

//Given a number, or '-', shifts it out to the display
void postNumber(byte number, boolean decimal)
{
  //       ---   A
  //     /   /   F, B
  //     ---     G
  //   /   /     E, C
  //   ---  .    D, DP

  #define a  1<<0
  #define b  1<<6
  #define c  1<<5
  #define d  1<<4
  #define e  1<<3
  #define f  1<<1
  #define g  1<<2
  #define dp 1<<7

  byte segments = 0;

  switch (number)
  {
    case 1: segments = b | c; break;
    case 2: segments = a | b | d | e | g; break;
    case 3: segments = a | b | c | d | g; break;
    case 4: segments = f | g | b | c; break;
    case 5: segments = a | f | g | c | d; break;
    case 6: segments = a | f | g | e | c | d; break;
    case 7: segments = a | b | c; break;
    case 8: segments = a | b | c | d | e | f | g; break;
    case 9: segments = a | b | c | d | f | g; break;
    case 0: segments = a | b | c | d | e | f; break;
    case ' ': segments = 0; break;
    case 'c': segments = g | e | d; break;
    case '-': segments = g; break;
    case '_': segments = d; break;
  }

  if (decimal) segments |= dp;

  shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, segments);
  //Clock these bits out to the drivers
  // for (byte x = 0 ; x < 8 ; x++)
  // {
  //   digitalWrite(PIN_CLK, LOW);
  //   digitalWrite(PIN_DATA, segments & 1 << (7 - x));
  //   digitalWrite(PIN_CLK, HIGH); //Data transfers to the register on the rising edge of SRCK
  // }
}

//Takes a number and displays it with leading zeroes
void showNumber(float value)
{
  int number = reverse(abs(value)); 

  digitalWrite(PIN_LATCH, LOW);

  // update all digits of the display
  for (int x = 0 ; x < DISPLAY_SIZE ; x++)
  {
    byte remainder = number % 10;
    postNumber(remainder, false);
    number /= 10;
  }
  //Latch the current segment data
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void showRandom(){
  digitalWrite(PIN_LATCH, LOW);
  shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, random(128));
  //Latch the current segment data
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}

void blankDisplay(){
  digitalWrite(PIN_LATCH, LOW);
  for (int i=0; i<4; i++){
      shiftOut(PIN_DATA, PIN_CLK, MSBFIRST, 0);
  }
  digitalWrite(PIN_LATCH, HIGH); //Register moves storage register on the rising edge of RCK
}
