/*
 * Hue.h
 * 
 * Library to add a hue-compatible interface to your led-stripes.
 * It requires the diyHue bridge up and running.
 * 
 * GPL license -- 17.12.2019
 */
#ifndef Hue_h
#define Hue_h
 
#define LIGHT_VERSION 3.1
#define ENTERTAINMENT_TIMEOUT 1500    // millis
#define RGB_R 100      // light multiplier in percentage /R, G, B/
#define RGB_G 100
#define RGB_B 100
#define LEDS_NUM 277  //physical number of leds in the stripe

class HueApi {
  
  private:
    int transitiontime;
    bool inTransition;
    float maxDist;

    void convRgbToXy(int r, int g, int b, uint8_t bri, float X, float Y) { 
      float x, y, z;
      
      r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : r / 12.92;
      g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : g / 12.92;
      b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : b / 12.92;
    
      x = (r * 0.4124 + g * 0.3576 + b * 0.1805) / 0.95047;
      y = (r * 0.2126 + g * 0.7152 + b * 0.0722) / 1.00000;
      z = (r * 0.0193 + g * 0.1192 + b * 0.9505) / 1.08883;
    
      x = (x > 0.008856) ? pow(x, 1/3) : (7.787 * x) + 16/116;
      y = (y > 0.008856) ? pow(y, 1/3) : (7.787 * y) + 16/116;
      z = (z > 0.008856) ? pow(z, 1/3) : (7.787 * z) + 16/116;
    
      bri = (uint8_t) (116 * y) - 16;
      X = 500 * (x - y);
      Y = 200 * (y - z);
    }
        
    CRGB convXyToRgb(uint8_t bri, float x, float y) {
    /*  
      x = 0.95047 * ((x * x * x > 0.008856) ? x * x * x : (x - 16/116) / 7.787);
      y = 1.00000 * ((y * y * y > 0.008856) ? y * y * y : (y - 16/116) / 7.787);
      z = 1.08883 * ((z * z * z > 0.008856) ? z * z * z : (z - 16/116) / 7.787);
    
      r = x *  3.2406 + y * -1.5372 + z * -0.4986;
      g = x * -0.9689 + y *  1.8758 + z *  0.0415;
      b = x *  0.0557 + y * -0.2040 + z *  1.0570;
    
      r = (r > 0.0031308) ? (1.055 * Math.pow(r, 1/2.4) - 0.055) : 12.92 * r;
      g = (g > 0.0031308) ? (1.055 * Math.pow(g, 1/2.4) - 0.055) : 12.92 * g;
      b = (b > 0.0031308) ? (1.055 * Math.pow(b, 1/2.4) - 0.055) : 12.92 * b;
    
      return [Math.max(0, Math.min(1, r)) * 255, 
              Math.max(0, Math.min(1, g)) * 255, 
              Math.max(0, Math.min(1, b)) * 255]
     */
      int optimal_bri;
      float X, Y, Z, r, g, b, maxv;
      
      Y = y; X = x; Z = 1.0f - x - y;
    
      if (bri < 5)  optimal_bri = 5;  else  optimal_bri = bri;
    
      // sRGB D65 conversion
      r =  X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
      g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
      b =  X * 0.0557f - Y * 0.2040f + Z * 1.0570f;
    
      //  // Apply gamma correction v.2
      //  // Altering exponents at end can create different gamma curves
      //  r = r <= 0.04045f ? r / 12.92f : pow((r + 0.055f) / (1.0f + 0.055f), 2.4f);
      //  g = g <= 0.04045f ? g / 12.92f : pow((g + 0.055f) / (1.0f + 0.055f), 2.4f);
      //  b = b <= 0.04045f ? b / 12.92f : pow((b + 0.055f) / (1.0f + 0.055f), 2.4f);
    
      // Apply gamma correction v.1 (better color accuracy), try this first!
      r = r <= 0.0031308f ? 12.92f * r : (1.0f + 0.055f) * pow(r, (1.0f / 2.4f)) - 0.055f;
      g = g <= 0.0031308f ? 12.92f * g : (1.0f + 0.055f) * pow(g, (1.0f / 2.4f)) - 0.055f;
      b = b <= 0.0031308f ? 12.92f * b : (1.0f + 0.055f) * pow(b, (1.0f / 2.4f)) - 0.055f;
    
      maxv = 0;// calc the maximum value of r g and b
      if (r > maxv) maxv = r;
      if (g > maxv) maxv = g;
      if (b > maxv) maxv = b;
    
      if (maxv > 0) {// only if maximum value is greater than zero, otherwise there would be division by zero
        r /= maxv;   // scale to maximum so the brightest light is always 1.0
        g /= maxv;
        b /= maxv;
      }
    
      r = r < 0 ? 0 : r;
      g = g < 0 ? 0 : g;
      b = b < 0 ? 0 : b;
      r = r > 1.0f ? 1.0f : r;
      g = g > 1.0f ? 1.0f : g;
      b = b > 1.0f ? 1.0f : b;
      
      r = (int) (r * optimal_bri); 
      g = (int) (g * optimal_bri); 
      b = (int) (b * optimal_bri); 
    
      return CRGB(r, g, b);
    }
    
    CRGB convCtToRgb(uint8_t bri, int ct) {
      
      int hectemp = 10000 / ct;
      int r, g, b;
      
      if (hectemp <= 66) {
        r = 255;
        g = 99.4708025861 * log(hectemp) - 161.1195681661;
        b = hectemp <= 19 ? 0 : (138.5177312231 * log(hectemp - 10) - 305.0447927307);
      } else {
        r = 329.698727446 * pow(hectemp - 60, -0.1332047592);
        g = 288.1221695283 * pow(hectemp - 60, -0.0755148492);
        b = 255;
      }
    
      r = r > 255 ? 255 : r;
      g = g > 255 ? 255 : g;
      b = b > 255 ? 255 : b;
    
      // Apply multiplier for white correction
      r = r * RGB_R / 100;
      g = g * RGB_G / 100;
      b = b * RGB_B / 100;
    
      r = r * (bri / 255.0f); 
      g = g * (bri / 255.0f); 
      b = b * (bri / 255.0f);
    
      return CRGB(r, g, b);
    }
    
    float deltaE(uint8_t bri1, float x1, float y1, uint8_t bri2, float x2, float y2){
    // calculate the perceptual distance between colors in CIELAB
    // https://github.com/THEjoezack/ColorMine/blob/master/ColorMine/ColorSpaces/Comparisons/Cie94Comparison.cs
    
      uint8_t deltaBri;
      float deltaX, deltaY, deltaC, deltaH, deltaLKlsl, deltaCkcsc, deltaHkhsh, c1, c2, sc, sh, i;
      
      deltaBri = bri1 - bri2;
      deltaX = x1 - x2;
      deltaY = y1 - y2;
      c1 = sqrt(x1 * x1 + y1 * y1);
      c2 = sqrt(x2 * x2 + y2 * y2);
      deltaC = c1 - c2;
      deltaH = deltaX * deltaX + deltaY * deltaY - deltaC * deltaC;
      deltaH = deltaH < 0 ? 0 : sqrt(deltaH);
      sc = 1.0 + 0.045 * c1;
      sh = 1.0 + 0.015 * c1;
      deltaLKlsl = deltaBri / (1.0);
      deltaCkcsc = deltaC / (sc);
      deltaHkhsh = deltaH / (sh);
      i = deltaLKlsl * deltaLKlsl + deltaCkcsc * deltaCkcsc + deltaHkhsh * deltaHkhsh;
      
      return i < 0 ? 0 : sqrt(i);
    }
    
    float ColorDist(CRGB color1, CRGB color2) {
    
      uint8_t bri1, bri2;
      float x1, y1, x2, y2;
    
      convRgbToXy(color1.r, color1.g, color1.b, bri1, x1, y1);
      convRgbToXy(color2.r, color2.g, color2.b, bri2, x2, y2);
      
      return deltaE(bri1, x1, y1, bri2, x2, y2);
    }
    
    void nblendU8TowardU8( uint8_t& cur, const uint8_t target, uint8_t amount){
    // Helper function that blends one uint8_t toward another by a given amount
      
      uint8_t delta;
      
      if( cur == target) return;
      if( cur < target ) {
        delta = target - cur;
        delta = scale8_video( delta, amount);
        cur += delta;
      } else {
        delta = cur - target;
        delta = scale8_video( delta, amount);
        cur -= delta;
      }
    }
    
    CRGB fadeTowardColor( CRGB& cur, const CRGB& target, uint8_t amount){
    // Blend one CRGB color toward another CRGB color by a given amount.
    // Blending is linear, and done in the RGB color space.
    // This function modifies 'cur' in place.
      
      nblendU8TowardU8( cur.red,   target.red,   amount);
      nblendU8TowardU8( cur.green, target.green, amount);
      nblendU8TowardU8( cur.blue,  target.blue,  amount);
      return cur;
    }
    
    void fadeTowardColor( CRGB* L, const CRGB& bgColor, int M, int N, uint8_t fadeAmount){
    // Fade an entire array of CRGBs toward a given background color by a given amount
    // This function modifies the pixel array in place.
      
      for( uint16_t i = M; i <= N; i++) {
        this->fadeTowardColor( L[i], bgColor, fadeAmount);
      }
    }

  public:
    struct state {
      CRGB color;                     // new color
      CRGB* currentColor;             // pointer to one led representing the color of that virtual hue-light
      int stepLevel;                  // amount of transition in every loop
    
      int firstLed, lastLed;          // range of leds representing this emulated hue-light
      int lightNr;                    // hue light-nr
      bool lightState;                // true = On, false = Off
      uint8_t colorMode;              // 1 = xy, 2 = ct, 3 = hue/sat
      
      uint8_t bri;                    //brightness (1 - 254)
      int hue;                        // 0 - 65635
      uint8_t sat;                    // 0 - 254
      float x;                        // 0 - 1  x-coordinate of CIE color space
      float y;                        // 0 - 1  y-coordinate of CIE color space
      int ct;                         //color temperatur in mired (500 mired/2000 K - 153 mired/6500 K)
    };
    state* lights = new state[0];    //holds the emulated hue-lights
    String LightName_;
    uint8_t LightsCount_;         //number of emulated hue-lights
    uint16_t PixelPerLight_;         //number of leds forming one emulated hue-light
    uint8_t TransitionLeds_;      //number of 'space'-leds inbetween the emulated hue-lights; pixelCount must be divisible by this value
    int FirstHueLightNr_;         //first free number for the first hue-light (look in diyHue config.json)
    CRGB* leds_;
    byte* mac_;
    bool NeedSave;            // indecates, if we have unsaved changes
    
    // constructor
    HueApi();
    HueApi(CRGB* Leds, byte* mac, int FirstHueLightNr = 1) :  
           leds_(Leds), mac_(mac), FirstHueLightNr_(FirstHueLightNr) {
      maxDist = ColorDist(CRGB::White, CRGB::Black);
    }

    void setupLights(String LightName, uint8_t LightsCount, uint16_t PixelPerLight, uint8_t TransitionLeds) {
      
      int x;

      LightName_ = LightName;
      LightsCount_ = LightsCount;
      PixelPerLight_ = PixelPerLight;
      TransitionLeds_ = TransitionLeds;
      
      lights = new state[LightsCount_];
      
      for (uint8_t i = 0; i < LightsCount_; i++) {
        lights[i].firstLed = TransitionLeds_ / 2 + i * PixelPerLight_ + TransitionLeds_ * i;     //﻿=transitionLeds / 2 + i * lightLeds + transitionLeds * i
        lights[i].lastLed = lights[i].firstLed + PixelPerLight_;
        lights[i].lightNr = FirstHueLightNr_ + i;
        
        x = lights[i].firstLed + (int)((lights[i].lastLed - lights[i].firstLed) / 2);
        lights[i].currentColor = &leds_[x];
    
        lights[i].lightState = true;
        lights[i].color = CRGB::Yellow;
        
        processLightdata(i, 4);
      }   
    }
    
    void processLightdata(uint8_t light, float transitiontime) {
    
      float tmp;
      
      if (lights[light].colorMode == 1 && lights[light].lightState == true) {
        lights[light].color = convXyToRgb(lights[light].bri, lights[light].x, lights[light].y);
      } else if (lights[light].colorMode == 2 && lights[light].lightState == true) {
        lights[light].color = convCtToRgb(lights[light].bri, lights[light].ct);
      } else if (lights[light].lightState == false) {
        lights[light].color = CRGB(CRGB::Black);
      }
      
      transitiontime *= 17 - (PixelPerLight_ / 40);         //every extra led add a small delay that need to be counted
      if (lights[light].lightState) {
        tmp = ColorDist(*lights[light].currentColor, lights[light].color);
        tmp = tmp / maxDist * 255;
        lights[light].stepLevel =  (int) (tmp / transitiontime);
      } else {
        tmp = ColorDist(*lights[light].currentColor, CRGB::Black);
        tmp = tmp / maxDist * 255;
        lights[light].stepLevel = (int) (tmp / transitiontime);
      }
    }

    void lightEngine() {
      
      for (int light = 0; light < LightsCount_; light++) {
        
        if ( lights[light].lightState && lights[light].color != *lights[light].currentColor || *lights[light].currentColor > CRGB(CRGB::Black)) {
          inTransition = true;
          this->fadeTowardColor(leds_, lights[light].color, lights[light].firstLed, lights[light].lastLed, lights[light].stepLevel);
        } else {
          inTransition = false;
        }
      }
      if (inTransition) delay(6);
    }

    void apply_scene(uint8_t new_scene) {
    
      for (uint8_t light = 0; light < LightsCount_; light++) {
        if ( new_scene == 1) {
          lights[light].color = convCtToRgb(254, 346);
        } else if ( new_scene == 2) {
          lights[light].color = convCtToRgb(254, 233);
        }  else if ( new_scene == 3) {
          lights[light].color = convCtToRgb(254, 156);
        }  else if ( new_scene == 4) {
          lights[light].color = convCtToRgb(77, 367);
        }  else if ( new_scene == 5) {
          lights[light].color = convCtToRgb(254, 447);
        }  else if ( new_scene == 6) {
          lights[light].color = convXyToRgb(1, 0.561, 0.4042);
        }  else if ( new_scene == 7) {
          lights[light].color = convXyToRgb(203, 0.380328, 0.39986);
        }  else if ( new_scene == 8) {
          lights[light].color = convXyToRgb(112, 0.359168, 0.28807);
        }  else if ( new_scene == 9) {
          lights[light].color = convXyToRgb(142, 0.267102, 0.23755);
        }  else if ( new_scene == 10) {
          lights[light].color = convXyToRgb(216, 0.393209, 0.29961);
        } else {
          lights[light].color = convCtToRgb(144, 447);
        }
      }
    }

        
    String Detect() {
            
      String output;
      char macString[32] = {0};
      sprintf(macString, "%02X:%02X:%02X:%02X:%02X:%02X", mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
      DynamicJsonDocument root(1024);
      
      root["name"] = LightName_;
      root["lights"] = LightsCount_;
      root["protocol"] = "native_multi";
      root["modelid"] = "LCT015";
      root["type"] = "SK9822_strip";
      root["mac"] = macString;
      root["version"] = LIGHT_VERSION;
      
      serializeJson(root, output);
      return output;
    }

    String StateGet(String arg) {
      
      String output;
      uint8_t light;
      DynamicJsonDocument root(1024);
      JsonArray xy;
      float x, y;
      int bri;
    
      light = arg.toInt() - 1;
      
      root["on"] = lights[light].lightState;
      root["bri"] = lights[light].bri;
      root["ct"] = lights[light].ct;
      root["hue"] = lights[light].hue;
      root["sat"] = lights[light].sat;
      if (lights[light].colorMode == 1)
        root["colormode"] = "xy";
      else if (lights[light].colorMode == 2)
        root["colormode"] = "ct";
      else if (lights[light].colorMode == 3)
        root["colormode"] = "hs";
    
      convRgbToXy(lights[light].color.r, lights[light].color.g, lights[light].color.b, bri, x, y);
      xy = root.createNestedArray("xy");
      xy.add(lights[light].x);
      xy.add(lights[light].y);
      
      serializeJson(root, output);
      return output;
   }
      
    String StatePut(String arg) {
      
      String output;
      DynamicJsonDocument root(1024);
      DeserializationError error;
      JsonString key;
      JsonVariant values;
      int light, transitiontime;
      char* buffer[1024];
      
      error = deserializeJson(root, arg); 
      if (error) {
        return "FAIL. " + arg;
      } else {
        
        for (JsonPair state : root.as<JsonObject>()) {
          key = state.key();
          light = atoi(key.c_str()) - 1;
          values = state.value();
          transitiontime = 4;
    
          if (values.containsKey("xy")) {
            lights[light].x = values["xy"][0];
            lights[light].y = values["xy"][1];
            lights[light].colorMode = 1;
            lights[light].color = convXyToRgb(lights[light].bri, lights[light].x, lights[light].y);
          } else if (values.containsKey("ct")) {
            lights[light].ct = values["ct"];
            lights[light].colorMode = 2;
            lights[light].color = convCtToRgb(lights[light].bri, lights[light].ct);
          } else {
            if (values.containsKey("hue")) {
              lights[light].hue = values["hue"];
              lights[light].colorMode = 3;
            }
            if (values.containsKey("sat")) {
              lights[light].sat = values["sat"];
              lights[light].colorMode = 3;
            }
            lights[light].color = CHSV(lights[light].hue, lights[light].sat, lights[light].bri);
          }
    
          if (values.containsKey("on")) {
            if (values["on"]) {
              lights[light].lightState = true;
            } else {
              lights[light].lightState = false;
            }
          }
    
          if (values.containsKey("bri")) {
            lights[light].bri = values["bri"];
          }
    
          if (values.containsKey("bri_inc")) {
            lights[light].bri += (int) values["bri_inc"];
            if (lights[light].bri > 255) lights[light].bri = 255;
            else if (lights[light].bri < 1) lights[light].bri = 1;
          }
    
          if (values.containsKey("transitiontime")) {
            transitiontime = values["transitiontime"];
          }
    
          if (values.containsKey("alert") && values["alert"] == "select") {
            if (lights[light].lightState) {
              lights[light].color = CRGB::Black;
            } else {
              lights[light].color = CRGB::Blue;
            }
          }
          processLightdata(light, transitiontime);
          NeedSave = true;
        }
        
        serializeJson(root, output);
        return output;
      }
    }
      
};
#endif
