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
#include "./weatherfont.h"
#include <algorithm>

#include "mrss.h"
#include <time.h>
#include <pthread.h>

#include <curl/curl.h>

#include "json/json.h"

#define OPENWEATHER_API_KEY "xxx"

struct strings {
  char *ptr;
  size_t len;
};

void init_string(struct strings *s) {
  s->len = 0;
  s->ptr = (char*)malloc(s->len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct strings *s)
{
  size_t new_len = s->len + size*nmemb;
  s->ptr = (char*)realloc(s->ptr, new_len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "realloc() failed\n");
    exit(EXIT_FAILURE);
  }
  memcpy(s->ptr+s->len, ptr, size*nmemb);
  s->ptr[new_len] = '\0';
  s->len = new_len;

  return size*nmemb;
}


#define COLUMN_DRIVERS 30
#define MODULE_WIDTH 3

//#define DEBUGMODE
//#define HW_DEBUGMODE
//#define FPS_COUNTER
using namespace std;

const int latchPin = 7;

float refreshRate = 120.0f;
float numberOfRows = 24.0f;
float onTime = ((1.0f/refreshRate) / numberOfRows) * 1000000.0f;
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
        ret = mrss_parse_url_with_options_and_error ("https://news.google.com/news/rss/?ned=us&gl=US&hl=en", &data, NULL, &code);
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

void *GetWeather(void *weatherData)
{
    string weatherText = "";

    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if(curl) {
    struct strings s;
    init_string(&s);

    curl_easy_setopt(curl, CURLOPT_URL, (std::string("http://api.openweathermap.org/data/2.5/forecast?q=Redwood%20City&mode=json&units=imperial&cnt=1&appid=") + std::string(OPENWEATHER_API_KEY)).c_str() );
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);

    printf("%s\n", s.ptr);
    string jsonData = s.ptr;
    free(s.ptr);

    /* always cleanup */
    curl_easy_cleanup(curl);


    Json::Value root;   // will contains the root value after parsing.
    Json::Reader reader;

    bool parsingSuccessful = reader.parse( jsonData, root );

    int highTemperature = root["list"][0]["main"].get("temp_max", -1.0f).asInt();
    weatherText = std::to_string(highTemperature) + "F";

    string *nextString = (string *)weatherData;
    nextString->assign(weatherText);

    weatherData = (void*)nextString;

    pthread_exit(NULL);

    }
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

unsigned char* encodeLetters(const char* str, /*int* colors*/ int color, int length, int offset, int currentRow, int &lastFirstLetter, int &curWidthSum, std::map<char, std::pair<int, int> > &characterDictionary, const char* tinystr, int tinyLength, int tinyOffset, int tinyColor, const bool* fontArray, const int fontArrayWidth)
{
    unsigned char *buffer = new unsigned char[COLUMN_DRIVERS * 2 + 3];
    int offsetT = offset;
    if(offset < 0)
        offset = 0;

    int firstLetter = lastFirstLetter;
    int widthSum = curWidthSum;
    int nextLength = 0;

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

    int currentIndex = characterDictionary[str[stringPosition]].first+currentLetterPosition;
    int tinyCurrentIndex = tinyCharacterDictionary[tinystr[currentStringPositionTiny]].first+currentLetterPositionTiny;

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
            else if(stringPosition < length)
            {
                if((color == 1 || color == 3) && (currentRow < 16 || str[stringPosition] == 'Q' || str[stringPosition] == 'g' || str[stringPosition] == 'j' || str[stringPosition] == 'p' || str[stringPosition] == 'q' || str[stringPosition] == 'y' ))
                {
                    valR |= fontArray[currentRow * targaFontWidth + currentIndex];
                }

                if((color == 2 || color == 3) && (currentRow < 16 || str[stringPosition] == 'Q' || str[stringPosition] == 'g' || str[stringPosition] == 'j' || str[stringPosition] == 'p' || str[stringPosition] == 'q' || str[stringPosition] == 'y' ))
                {
                    valG |= fontArray[currentRow * targaFontWidth + currentIndex];
                }

                currentLetterPosition++;

                if(currentLetterPosition >= currentLetterLength)
                {
                    stringPosition++;
                    currentLetterLength = characterDictionary[str[stringPosition]].second;
                    currentLetterPosition = 0;
                }
                currentIndex = characterDictionary[str[stringPosition]].first+currentLetterPosition;
            }
            
            if(tinyOffset < 0)
            {
                tinyOffset++;
            }
            else if(currentStringPositionTiny < tinyLength && currentRow >= 16 )
            {
                if(tinyColor == 1 || tinyColor == 3)
                {
                    if(currentRow == 16 && tinystr[currentStringPositionTiny] == '4'){}
                    else if(tinystr[currentStringPositionTiny] != ' ')
                        valR |= targafont[currentRow][tinyCurrentIndex];
                }

                if(tinyColor == 2 || tinyColor == 3)
                {
                    if(currentRow == 16 && tinystr[currentStringPositionTiny] == '4'){}
                    else if(tinystr[currentStringPositionTiny] != ' ')
                        valG |= targafont[currentRow][tinyCurrentIndex];
                }

                currentLetterPositionTiny++;

                if(currentLetterPositionTiny >= currentLetterLengthTiny)
                {
                    currentStringPositionTiny++;
                    currentLetterLengthTiny = tinyCharacterDictionary[tinystr[currentStringPositionTiny]].second;
                    currentLetterPositionTiny = 0;
                }

                tinyCurrentIndex = tinyCharacterDictionary[tinystr[currentStringPositionTiny]].first+currentLetterPositionTiny;
            }
/*
            if(k == 18 && currentRow >= 16)
            {
                valR |= (weatherIcons[currentRow-16][i] == 1);
                valR |= (weatherIcons[currentRow-16][i] == 3);
                valG |= (weatherIcons[currentRow-16][i] == 2);
                valG |= (weatherIcons[currentRow-16][i] == 3);
            }
            if(k == 19 && currentRow >= 16)
            {
                valR |= (weatherIcons[currentRow-16][i+8] == 1);
                valR |= (weatherIcons[currentRow-16][i+8] == 3);
                valG |= (weatherIcons[currentRow-16][i+8] == 2);
                valG |= (weatherIcons[currentRow-16][i+8] == 3);
            }
            if(k == 20 && currentRow >= 16)
            {
                valR |= (weatherIcons[currentRow-16][i+16] == 1);
                valR |= (weatherIcons[currentRow-16][i+16] == 3);
                valG |= (weatherIcons[currentRow-16][i+16] == 2);
                valG |= (weatherIcons[currentRow-16][i+16] == 3);
            }
            if(k == 21 && currentRow >= 16)
            {
                valR |= (weatherIcons[currentRow-16][i+24] == 1);
                valR |= (weatherIcons[currentRow-16][i+24] == 3);
                valG |= (weatherIcons[currentRow-16][i+24] == 2);
                valG |= (weatherIcons[currentRow-16][i+24] == 3);
            }
            if(k == 22 && currentRow >= 16)
            {
                valR |= (weatherIcons[currentRow-16][i+32] == 1);
                valR |= (weatherIcons[currentRow-16][i+32] == 3);
                valG |= (weatherIcons[currentRow-16][i+32] == 2);
                valG |= (weatherIcons[currentRow-16][i+32] == 3);
            }
*/
        }
#ifdef HW_DEBUGMODE
        if(offset % 100 >=0 && offset % 100 < 50)
        {
            valG = 0xFF;
            valR = 0xFF;
        }
#endif

        buffer[(MODULE_WIDTH-1)-(k%MODULE_WIDTH) + int(floor((COLUMN_DRIVERS-1-k)/MODULE_WIDTH))*MODULE_WIDTH*2] = reverseBits(valR);
        buffer[(MODULE_WIDTH-1)-(k%MODULE_WIDTH) + int(floor((COLUMN_DRIVERS-1-k)/MODULE_WIDTH))*MODULE_WIDTH*2+MODULE_WIDTH] = reverseBits(valG);
        
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
    if(wiringPiSPISetup (0, 5000000) < 0)
    {
     //       printf("Error!\n");
        return -1;
    }

    pinMode(latchPin, OUTPUT);


    unsigned char clear[COLUMN_DRIVERS * 2 + 3];
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

    std::map<char, std::pair<int, int> > *characterDictionary = &targaCharacterDictionary;

//    transform(newsString.begin(), newsString.end(),newsString.begin(), ::toupper);

    int stringPixelLength = 0;
    for(int i = 0; i < newsString.length(); i++)
    {
        stringPixelLength += (*characterDictionary)[newsString[i]].second;
    }

    int lastFirstLetter = 0;
    int curWidthSum = 0;

    pthread_t thread;
    string *nextString = new string();
    nextString->assign("");
    pthread_create(&thread, NULL, GetRSSFeed, (void *) nextString);
    printf("%s\n\n", newsString.c_str());

    string weatherString = "";


    pthread_t weatherThread;
    string *nextWeatherString = new string();
    nextWeatherString->assign("");
    pthread_create(&weatherThread, NULL, GetWeather, (void *) nextWeatherString);
    pthread_join(weatherThread, NULL);
    weatherString = nextWeatherString->c_str();
    delete nextWeatherString;
    nextWeatherString = new string();
    nextWeatherString->assign("");

    time_t rawtime;
    struct tm * timeinfo;
    time (&rawtime);
    timeinfo = localtime (&rawtime);
    std::string currentTime = asctime(timeinfo);
    currentTime.erase(currentTime.end()-1);

    unsigned long delta = millis();

    int fps = 0;
    int fpsCounter = 0;
    unsigned long fpsTime = millis();


    int weatherTimer = millis();
    while(true)
    {
        if(millis() - weatherTimer >= 3600000)
        {
            weatherTimer = millis();
            weatherString = nextWeatherString->c_str();
            delete nextWeatherString;
            nextWeatherString = new string();
            nextWeatherString->assign("");
            pthread_join(weatherThread, NULL);
            pthread_create(&weatherThread, NULL, GetWeather, (void *) nextWeatherString);
        }

        //If the end of the news text is reached, load the next news source's text
        if(offset >= stringPixelLength)
        {
            offset = -256.0f;
            lastFirstLetter = 0;
            curWidthSum = 0;

            newsString = nextString->c_str();

//            transform(newsString.begin(), newsString.end(),newsString.begin(), ::toupper);
            delete nextString;
            nextString = new string();
            nextString->assign("");

            //if(newsString == "NO INTERNET CONNECTION!")
            pthread_join(thread, NULL);

            pthread_create(&thread, NULL, GetRSSFeed, (void *) nextString);

            stringPixelLength = 0;
            for(int i = 0; i < newsString.length(); i++)
            {
                stringPixelLength += (*characterDictionary)[newsString[i]].second;
            }
            printf("%s\n\n", newsString.c_str());

            if(newsSource == 0)
                color = 2;
            else if(newsSource == 1)
                color = 3;
            else
                color = 1;

            //Get Time Info
            time (&rawtime);
            timeinfo = localtime (&rawtime);
            currentTime = asctime(timeinfo);
            currentTime.erase(currentTime.end()-1);

#ifdef FPS_COUNTER
            currentTime.append(" ");
            currentTime.append(to_string(fps));
#endif

            switch(color)
            {
                case 1:
                currentTime.append("                    CNN");
                break;
                case 3:
                currentTime.append("            Google News");
                break;
                case 2:
                currentTime.append("              Engadget");
                break;
            }
        }





        //Render the frame to the display
        unsigned long stTime = 0;
        rows = 0x800000;
        for(int row = 0; row < 24; row++)
        {
            

            unsigned char* buffer = encodeLetters(newsString.c_str(), color, newsString.length(), offset, row, lastFirstLetter, curWidthSum, *characterDictionary, currentTime.c_str(), currentTime.length(), 0, 2, *targafont, targaFontWidth);
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

            digitalWrite(latchPin, LOW);

            wiringPiSPIDataRW(0, buffer, COLUMN_DRIVERS * 2 + 3);
            delete buffer;

            rows = rows >> 1;

            unsigned long endTime = micros();

            if(endTime - stTime < onTime)
            {
                delt = endTime - stTime;
                delayMicroseconds(onTime - (endTime-stTime)); 
            }

            digitalWrite(latchPin, HIGH);
            digitalWrite(latchPin, LOW);

            stTime = micros();
        }
        wiringPiSPIDataRW(0, clear, COLUMN_DRIVERS * 2 + 3);
        digitalWrite(latchPin, HIGH);
        digitalWrite(latchPin, LOW);




        //Offset the news text on the display every so often
        if(millis() - delta > 6)
        {
            delta = millis();  
            offset+=1.0f;

            //Get Time Info
            time (&rawtime);
            timeinfo = localtime (&rawtime);
            currentTime = asctime(timeinfo);
            currentTime.erase(currentTime.end()-1);

#ifdef FPS_COUNTER
            currentTime.append(" ");
            currentTime.append(to_string(fps));
#endif
            currentTime.append(" ");
            currentTime.append(weatherString);
            switch(color)
            {
                case 1:
                currentTime.append("                    CNN");
                break;
                case 3:
                currentTime.append("            Google News");
                break;
                case 2:
                currentTime.append("               Engadget");
                break;
            }
        }

        if(millis() - fpsTime >= 1000)
        {
            fpsTime = millis();
            fps = fpsCounter;
            fpsCounter = 0;
        }
        else
        {
            fpsCounter++;
        }

    }
}

