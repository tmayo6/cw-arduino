/*
 * Background-type CW message player
 * 
 * By Tom Mayo - N1MU - http://www.2ub.org/
 *
 */

//#define MODE_MESSAGE
#undef MODE_MESSAGE

//#include <stdio.h>
#include <string.h>

#define CW_INIT 0
#define CW_IDLE 1
#define CW_START 2
#define CW_PLAYING 3
#define CW_WAITING 4

/* Set to 1 for excessive debug message printing */
static int debug = 0;

/* pointer to character being played */
char *ptr;

/* message to play */
#ifdef MODE_MESSAGE
char message[32];
#else
char m;
#endif

/* state of machine */
int state;

/* timer indicates when to play message */
int timer;

/* period to reset timer with after expiration */
static int timeout_period = 100;

/* cntr indicates how long to play tone or space */
int cntr;

/* play tone if 1, space if 0 */
int play;

/* encoded version of character being played */
unsigned char widget;

/*
 * MORSE CODE encoding...
 *
 * morse characters are encoded in a single byte, bitwise, LSB to MSB.
 * 0 = dit, 1 = dah.  the byte is shifted out to the right, until only
 * a 1 remains.  characters with more than 7 elements (error) cannot be
 * sent.
 * 
 * Note:  + is AR, # is AS, * is SK, > is BK
 *
 * Another Note:  I did not invent this encoding scheme, but I'm not
 * sure who to credit.  It's pretty nice because the more frequently
 * used characters are shorter and more toward the beginning of the list
 * and therefore quicker to look up.  Also, the table is TINY!
 *
 */

char valid[] = "etinamsdrgukwohblzfcpvxqyj56#78/+(94=3210:?\";@'-*._),!$>";

unsigned char morsecode[] = {
/* ; e .       00000010 */  0x02,
/* ; t -       00000011 */  0x03,
/* ; i ..      00000100 */  0x04,
/* ; n -.      00000101 */  0x05,
/* ; a .-      00000110 */  0x06,
/* ; m --      00000111 */  0x07,
/* ; s ...     00001000 */  0x08,
/* ; d -..     00001001 */  0x09,
/* ; r .-.     00001010 */  0x0a,
/* ; g --.     00001011 */  0x0b,
/* ; u ..-     00001100 */  0x0c,
/* ; k -.-     00001101 */  0x0d,
/* ; w .--     00001110 */  0x0e,
/* ; o ---     00001111 */  0x0f,
/* ; h ....    00010000 */  0x10,
/* ; b -...    00010001 */  0x11,
/* ; l .-..    00010010 */  0x12,
/* ; z --..    00010011 */  0x13,
/* ; f ..-.    00010100 */  0x14,
/* ; c -.-.    00010101 */  0x15,
/* ; p .--.    00010110 */  0x16,
/* ; v ...-    00011000 */  0x18,
/* ; x -..-    00011001 */  0x19,
/* ; q --.-    00011011 */  0x1b,
/* ; y -.--    00011101 */  0x1d,
/* ; j .---    00011110 */  0x1e,
/* ; 5 .....   00100000 */  0x20,
/* ; 6 -....   00100001 */  0x21,
/* ; # .-...   00100010 */  0x22,
/* ; 7 --...   00100011 */  0x23,
/* ; 8 ---..   00100111 */  0x27,
/* ; / -..-.   00101001 */  0x29,
/* ; + .-.-.   00101010 */  0x2a,
/* ; ( -.--.   00101101 */  0x2d,
/* ; 9 ----.   00101111 */  0x2f,
/* ; 4 ....-   00110000 */  0x30,
/* ; = -...-   00110001 */  0x31,
/* ; 3 ...--   00111000 */  0x38,
/* ; 2 ..---   00111100 */  0x3c,
/* ; 1 .----   00111110 */  0x3e,
/* ; 0 -----   00111111 */  0x3f,
/* ; : ---...  01000111 */  0x47,
/* ; ? ..--..  01001100 */  0x4c,
/* ; " .-..-.  01010010 */  0x52,
/* ; ; -.-.-.  01010101 */  0x55,
/* ; @ .--.-.  01010110 */  0x56,
/* ; ' .----.  01011110 */  0x5e,
/* ; - -....-  01100001 */  0x61,
/* ; * ...-.-  01101000 */  0x68,
/* ; . .-.-.-  01101010 */  0x6a,
/* ; _ ..--.-  01101100 */  0x6c,
/* ; ) -.--.-  01101101 */  0x6d,
/* ; , --..--  01110011 */  0x73,
/* ; ! -.-.--  01110101 */  0x75,
/* ; $ ...-..- 11001000 */  0xc8,
/* ; > -...-.- 11010001 */  0xd1
};

/* function to return the encoded version of a given char */
unsigned char morse(char c) {
  int i;

  for (i = 0; i < strlen(valid); i++) {
    if (valid[i] == c)
      break;
  }

  if (i != strlen(valid))
    return(morsecode[i]);
  else
    return(0);
}

/* function to determine how long to play tone or space */
void sel_action(unsigned char widget, int *cntr, int *play) {

  if (widget == 0) { /* play 2 dits of space (5 more will get added) */

    *play = 0;
    *cntr = 2;

  } else { /* play tone for 3 dits if dah or 1 dit if dit */

    *play = 1;
    if (widget & 1)
      *cntr = 3;
    else
      *cntr = 1;
  }
}

/* function to do what is necessary if tone is on */
void output_on(void) {
  Serial.print("-");
  digitalWrite(13, HIGH);
}

/* function to do what is necessary if tone is off */
void output_off(void) {
  Serial.print("_");
  digitalWrite(13, LOW);
}

/* function to be called periodically to run the CW output */
void isr(void) {

  switch(state) { /* branch based on what is going on */

  case CW_INIT: /* reset */
    timer = 1;
    cntr = play = 0;
    Serial.println("");
    /* fall through */
  
  case CW_IDLE: /* CW_WAITING for timer to timeout */
    if (--timer != 0)
      break;

#ifdef MODE_MESSAGE
  case CW_START: /* get message and begin CW_PLAYING */
    ptr = message;
    ////if (debug) printf("first ptr = %s\n", ptr);
    ////if (debug) printf("first *ptr = %c\n", *ptr);
    widget = morse(*ptr);
    sel_action(widget, &cntr, &play);
    state = CW_PLAYING;
    break;

  case CW_WAITING: /* CW_WAITING for end of message or space */

    if (cntr == 0) {
      if (*(++ptr) == '\0') { /* End of message */
        timer = timeout_period;
        state = CW_IDLE;
      } else { /* Next character */
        widget = morse(*ptr);
        sel_action(widget, &cntr, &play);
        state = CW_PLAYING;
      }
    }
    break;
#else
  case CW_START: /* get message and begin CW_PLAYING */
    widget = morse(m);
    sel_action(widget, &cntr, &play);
    state = CW_PLAYING;
    break;

  case CW_WAITING: /* CW_WAITING for end of message or space */

    if (cntr == 0) {
        state = CW_IDLE;
    }
    break;
#endif

  case CW_PLAYING: /* In the midst of CW_PLAYING morse code for a char */

    if (cntr == 0) { /* Done with this symbol or space */

      if (play == 0) { /* Was CW_PLAYING space, go ahead and update widget */

        widget >>= 1;

        if (widget <= 1) { /* End of this character */

          state = CW_WAITING;
          cntr = 2;
          play = 0;

        } else { /* Continue CW_PLAYING the character */

          sel_action(widget, &cntr, &play);
          state = CW_PLAYING;
        }
      } else { /* Was CW_PLAYING tone */

  if (widget == 1) { /* Done with char, play char space */

    cntr = 3;
    play = 0;

  } else { /* Done with symbol, play symbol space */

    cntr = 1;
    play = 0;
  }
      }
    } else { /* cntr has not expired, continue */

      state = CW_PLAYING;
    }
    break;

  }

  //if (debug) printf("cntr = %d\n", cntr);
  //if (debug) printf("play = %d\n", play);
  //if (debug) printf("widget = 0x%2.2x\n", widget);
  //if (debug) printf("ptr = %s\n", ptr);
  //if (debug) printf("message = %s\n", message);

  /* decrement symbol counter */
  --cntr;

  /* Turn tone on or off */
  if (play)
    output_on();
  else
    output_off();

  return;
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);

#ifdef MODE_MESSAGE
  strcpy(message, "v v v de n1mu/b fn12");
  state = CW_INIT;
#else
  state = CW_IDLE;
#endif

}

void loop() {
  // put your main code here, to run repeatedly:

#ifdef MODE_MESSAGE
  if (state != CW_IDLE) {
    isr();
    delay(80); // totally uncalibrated speed.  Your mileage may vary.  
    // TODO: See if we could do this in an interrupt.
  } else {
    state = CW_INIT;
    delay(3000);
  }

#else

  if (state == CW_IDLE) {
    if (Serial.available() > 0) {
      m = Serial.read();

      if (m != -1) {
        state = CW_INIT;
      }
    }
  } else {
    isr();
    delay(50);
  }
#endif

}
