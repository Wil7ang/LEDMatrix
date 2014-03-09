#include <string.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <cstdlib>
#include <wiringPi.h>
#include <wiringShift.h>
#include <wiringPiSPI.h>
#include <cmath>
#include "./sansfont.h"
#include "./targafont.h"
#include <algorithm>

#include "mrss.h"
#include <time.h>
#include <pthread.h>

#define COLUMN_DRIVERS 30
#define MODULE_WIDTH 3

//#define DEBUGMODE
#define HW_DEBUGMODE
using namespace std;

const int latchPin = 7;

float refreshRate = 60;
float numberOfRows = 24;
float onTime = ((1.0/refreshRate) / numberOfRows) * 1000000;
unsigned long delt = 0;

int newsSource = 0;
int color = 2;
        
void *GetRSSFeed(void *newsData)
{
    string *nextString = (string *)newsData;
    string newsString = "";

    mrss_t *data;
    CURLcode code;
    mrss_item_t *item;
    mrss_error_t ret;
    switch(newsSource)
    {
        case 0:
        ret = mrss_parse_url_with_options_and_error ("https://news.google.com/?output=rss", &data, NULL, &code);
        newsSource++;
        break;

        case 1:
        ret = mrss_parse_url_with_options_and_error ("http://rss.cnn.com/rss/cnn_topstories.rss", &data, NULL, &code);
        newsSource++;
        break;

        case 2:
        ret = mrss_parse_url_with_options_and_error ("http://www.engadget.com/rss.xml", &data, NULL, &code);
        newsSource = 0;
        break;
    }

    /*else
    {
        ret = mrss_parse_url_with_options_and_error ("https://www.facebook.com/feeds/notifications.php?id=679469283&viewer=679469283&key=AWhY-tGsVPkIT3mj&format=rss20", &data, NULL, &code);
        newsSource = 0;
    }*/

    if(!ret)
    {
        item = data->item;
        
        while(item)
        {
            newsString += item->title;
            newsString += "   ";
            item = item->next;
        }

        nextString->assign(newsString);
    }
    else
        nextString->assign("No internet connection!");
    newsData = (void *) nextString;

    mrss_free(data);

    pthread_exit(NULL);
}

const char *byte_to_binary(int x)
{
    static char b[9];
    b[0] = '\0';

    int z;
    for (z = 128; z > 0; z >>= 1)
    {
        strcat(b, ((x & z) == z) ? "1" : "0");
    }

    return b;
}



char encodebool(bool* arr, int startPos, int dataWidth)
{
    char val = 0x0;
    val |= arr[(startPos)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 1)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 2)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 3)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 4)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 5)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 6)%dataWidth];
    val <<= 1;
    val |= arr[(startPos + 7)%dataWidth];
    return val;
}

char reverseBits(char x)
{
    x = (x & 0xF0) >> 4 | (x & 0x0F) << 4;
    x = (x & 0xCC) >> 2 | (x & 0x33) << 2;
    x = (x & 0xAA) >> 1 | (x & 0x55) << 1;
    return x;
}

unsigned char* encodeLetters(const char* str, /*int* colors*/ int color, int length, int offset, int currentRow, int &lastFirstLetter, int &curWidthSum, std::map<char, std::pair<int, int> > &characterDictionary, const char* tinystr, int tinyLength, int tinyOffset, int tinyColor)
{
    unsigned char *buffer = new unsigned char[COLUMN_DRIVERS * 2 + 3];
    int offsetT = offset;
    if(offset < 0)
        offset = 0;

    int firstLetter = lastFirstLetter;
    int widthSum = curWidthSum;
    int nextLength = 0;
    //rintf("%i\n", firstLetter);
    for(int i = firstLetter; i < length-1; i++)
    {
        nextLength = characterDictionary[str[i]].second;
        if(widthSum+nextLength > offset)
        {
            break;
        }
        else
        {
            firstLetter++;
            widthSum += nextLength;
        }
    }

    lastFirstLetter = firstLetter;
    curWidthSum = widthSum;

    char valR = 0x0;
    char valG = 0x0;

    int stringPosition = firstLetter;
    int currentLetterLength = characterDictionary[str[firstLetter]].second;
    int currentLetterPosition = offset - widthSum;

    int currentLetterPositionTiny = 0;
    int currentLetterLengthTiny = tinyCharacterDictionary[tinystr[0]].second;
    int currentStringPositionTiny = 0;

    for(int k = 0; k < COLUMN_DRIVERS; k++)
    {
        for(int i = 0; i < 8; i++)
        {
            valR <<= 1;
            valG <<= 1;
            if(offsetT < 0)
            {
                offsetT++;
            }
            else if(stringPosition < length && (currentRow < 16 || str[stringPosition] == 'Q' || str[stringPosition] == 'g' || str[stringPosition] == 'j' || str[stringPosition] == 'p' || str[stringPosition] == 'q' || str[stringPosition] == 'y' ))
            {
                if(color == 1 || color == 3)
                {
                    valR |= targafont[currentRow][characterDictionary[str[stringPosition]].first+currentLetterPosition];
                }

                if(color == 2 || color == 3)
                {
                    valG |= targafont[currentRow][characterDictionary[str[stringPosition]].first+currentLetterPosition];
                }

                currentLetterPosition++;

                if(currentLetterPosition >= currentLetterLength)
                {
                    stringPosition++;
                    currentLetterLength = characterDictionary[str[stringPosition]].second;
                    currentLetterPosition = 0;
                }
            }
            
            if(tinyOffset < 0)
            {
                tinyOffset++;
            }
            else if(currentStringPositionTiny < tinyLength && currentRow >= 16)
            {
                if(tinyColor == 1 || tinyColor == 3)
                {
                    if(currentRow == 16 && tinystr[currentStringPositionTiny] == '4'){}
                    else
                        valR |= targafont[currentRow][tinyCharacterDictionary[tinystr[currentStringPositionTiny]].first+currentLetterPositionTiny];
                }

                if(tinyColor == 2 || tinyColor == 3)
                {
                    if(currentRow == 16 && tinystr[currentStringPositionTiny] == '4'){}
                    else
                        valG |= targafont[currentRow][tinyCharacterDictionary[tinystr[currentStringPositionTiny]].first+currentLetterPositionTiny];
                }

                currentLetterPositionTiny++;

                if(currentLetterPositionTiny >= currentLetterLengthTiny)
                {
                    currentStringPositionTiny++;
                    currentLetterLengthTiny = tinyCharacterDictionary[tinystr[currentStringPositionTiny]].second;
                    currentLetterPositionTiny = 0;
                }
            }
        }

#ifdef HW_DEBUGMODE
        if(offset % 100 >=0 && offset % 100 < 20)
        {
            valG = 0xFF;
            valR = 0xFF;
        }
#endif

        buffer[(MODULE_WIDTH-1)-(k%MODULE_WIDTH) + int(floor((COLUMN_DRIVERS-1-k)/MODULE_WIDTH))*MODULE_WIDTH*2] = reverseBits(valG);
        buffer[(MODULE_WIDTH-1)-(k%MODULE_WIDTH) + int(floor((COLUMN_DRIVERS-1-k)/MODULE_WIDTH))*MODULE_WIDTH*2+MODULE_WIDTH] = reverseBits(valR);
        
        valR = 0x0;
        valG = 0x0;
    }
    
    return buffer;
}

struct newsData
{
    string* newsString;
    int stringLength;
};

int main()
{
    wiringPiSetup ();
    if(wiringPiSPISetup (0, 1500000) < 0)
    {
     //       printf("Error!\n");
        return -1;
    }

    pinMode(latchPin, OUTPUT);


    unsigned char clear[COLUMN_DRIVERS * 2 + 3];// = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0,0};// = {0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF};
    for(int i = 0; i < COLUMN_DRIVERS * 2 + 3; i++)
    {
        if(i < COLUMN_DRIVERS * 2)
            clear[i] = 0;
        else
            clear[i] = 0xFF;
    }
    
    wiringPiSPIDataRW (0, clear, COLUMN_DRIVERS * 2 + 3);

    digitalWrite(latchPin, HIGH);
    digitalWrite(latchPin, LOW);

    unsigned long bleh = 1;
    unsigned int rows = 0x800000;

    int currentRow = 0;

    float offset = -256.0f;

    digitalWrite(latchPin, LOW);
    
    mrss_t *data;
    CURLcode code;
    mrss_item_t *item;
    mrss_error_t ret = mrss_parse_url_with_options_and_error ("https://news.google.com/?output=rss", &data, NULL, &code);
    string newsString = "";

    if(!ret)
    {
        item = data->item;
        
        while(item)
        {
            newsString += item->title;
            newsString += "   ";
            item = item->next;
        }
    }
    else
        newsString = "No internet connection!";

    transform(newsString.begin(), newsString.end(),newsString.begin(), ::toupper);

    int stringPixelLength = 0;
    for(int i = 0; i < newsString.length(); i++)
    {
        stringPixelLength += targaCharacterDictionary[newsString[i]].second;
    }

    int lastFirstLetter = 0;
    int curWidthSum = 0;

    pthread_t thread;
    string *nextString = new string();
    nextString->assign("");
    pthread_create(&thread, NULL, GetRSSFeed, (void *) nextString);
    printf("%s\n\n", newsString.c_str());

    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime (&rawtime);
    std::string currentTime = asctime(timeinfo);
    currentTime.erase(currentTime.end()-1);

    unsigned long delta = millis();
    while(true)
    {
        
        unsigned long stTime = micros();
        if(currentRow == 24)
        {
            rows = 0x800000;
            currentRow = 0;
        }

        if(offset >= stringPixelLength)
        {
            offset = -256.0f;
            lastFirstLetter = 0;
            curWidthSum = 0;

            newsString = nextString->c_str();

            transform(newsString.begin(), newsString.end(),newsString.begin(), ::toupper);
            delete nextString;
            nextString = new string();
            nextString->assign("");

            //if(newsString == "NO INTERNET CONNECTION!")
            pthread_join(thread, NULL);

            pthread_create(&thread, NULL, GetRSSFeed, (void *) nextString);

            stringPixelLength = 0;
            for(int i = 0; i < newsString.length(); i++)
            {
                stringPixelLength += targaCharacterDictionary[newsString[i]].second;
            }
            printf("%s\n\n", newsString.c_str());

            if(newsSource == 0)
                color = 2;
            else if(newsSource == 1)
                color = 3;
            else
                color = 1;
        }

        digitalWrite(latchPin, LOW);

        //char *str = "Red, green, yellow";
        
        

        int colors[18] = { 1,1,1,1,0,2,2,2,2,2,2,0,3,3,3,3,3,3};



        

        unsigned char* buffer = encodeLetters(newsString.c_str(), color, newsString.length(), offset, currentRow, lastFirstLetter, curWidthSum, targaCharacterDictionary, currentTime.c_str(), currentTime.length(), -54, 1);
        buffer[COLUMN_DRIVERS * 2] = reverseBits(~rows);
        buffer[COLUMN_DRIVERS * 2 + 1] = reverseBits(~rows>>8);
        buffer[COLUMN_DRIVERS * 2 + 2] = reverseBits(~rows>>16);

#ifdef DEBUGMODE
        for(int i = 0; i < COLUMN_DRIVERS * 2 + 3; i++)
        {
            printf("%s ", byte_to_binary(buffer[i]));
        }
        printf("\n");
#endif

        wiringPiSPIDataRW(0, buffer, COLUMN_DRIVERS * 2 + 3);
        delete buffer;

        rows = rows >> 1;

        digitalWrite(latchPin, HIGH);
        digitalWrite(latchPin, LOW);
        
        currentRow++;

        
        if(millis() - delta > 6 && currentRow == 24)
        {
            delta = millis();  
            offset+=6.0f;

            //Get Time Info
            time (&rawtime);
            timeinfo = localtime (&rawtime);
            currentTime = asctime(timeinfo);
            currentTime.erase(currentTime.end()-1);
        }

        unsigned long endTime = micros();
        if(endTime - stTime < onTime)
        {
            delt = endTime - stTime;
            delayMicroseconds(onTime - (endTime-stTime)); 
        }
    }
}

