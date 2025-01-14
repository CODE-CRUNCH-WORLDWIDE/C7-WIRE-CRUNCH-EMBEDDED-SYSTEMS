# Course Content

## Topic: Programming Basics
- **Week Dates:** January 6, 2025 - January 10, 2025
- **Unit:** Week 0 - Overview of C7
- **Link to Slides:** [Programming Basics Slides](https://docs.google.com)

---

Sample text for instructions

### **Sample Code: C**
```c
#include <stdio.h>

// C++ code
//

#define RED_PIN 9
#define BLUE_PIN 10
#define GREEN_PIN 11

void setup()
{
  pinMode(RED_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
 
}

int red;
int blue;
int green;

void loop()
{
  red = 255;
  blue = 0;
  green = 0;
  for(int i = 0; i <= 255; i+=5){
    digitalWrite(RED_PIN, red-i);
    digitalWrite(BLUE_PIN, blue);

  }

  red=0;
  blue = 255;
  green = 0;
  for(int i = 0; i <= 255; i += 5){
    digitalWrite(BLUE_PIN, blue-i);
    digitalWrite(GREEN_PIN, green);

  }

  red = 0; 
  blue = 0;
  green = 255;
  for(int i = 0; i <= 255; i +=5){
    digitalWrite(GREEN_PIN, green -i);
    digitalWrite(RED_PIN, red);

  }

}
```
