#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

typedef NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> MyNeoPixelBus;

class PixelTrail {
    public:
        PixelTrail(float _color);
        void draw(MyNeoPixelBus& strip, NeoGamma<NeoGammaTableMethod> colorGamma, float brightness);
        void randomize();
        float hue;          // 0.0 .. 360.0
        float position;     // 0.0 .. 1.0
        float speed;        // very small values, 0.005 .. 0.02
        int trail_size;     // number of pixels
};

PixelTrail::PixelTrail(float _color){
    hue = _color;
    randomize();
}

void PixelTrail::randomize(){
    this->speed = random(100)/20000.0;
    this->position = random(1.0);
    this->trail_size = 2+random(7);
}

// Draws a trail of pixels that gradually fades out in one direction
void PixelTrail::draw(MyNeoPixelBus& strip, NeoGamma<NeoGammaTableMethod> colorGamma, float brightness){
    int last_pixel = strip.PixelCount()-5;
    float loc = last_pixel*position; // translated index of the object, within the LED strip, as a floating point value 
    int index = floor(loc)+1;
    // draw leading LED, less intense based on distance
    if (index >= 0 && index < last_pixel){
        RgbColor c = HslColor(hue, 1.0f, abs(loc-floor(loc))*brightness);
        RgbColor blended = RgbColor::LinearBlend(c, strip.GetPixelColor(index), 0.5);
        strip.SetPixelColor(index, colorGamma.Correct(blended)); 
    }
    // draw the "trail"
    for (int i=0; i<trail_size+1; i++){  
        int index = constrain(floor(loc)-i, 0, last_pixel); // the pysical LED where the point is
        if (index >= 0 && index < last_pixel){
            float level = 1.0 - abs((loc-index)/(trail_size*1.0)); // intensity by distance from real pixel to virtual point
            RgbColor c = HslColor(hue, 1.0f, max(0.0f, level)*brightness);
            RgbColor blended = RgbColor::LinearBlend(c, strip.GetPixelColor(index), 0.5);
            strip.SetPixelColor(index, colorGamma.Correct(blended)); 
        }
    }
    // update position
    position += speed;
    if (position > 1.2){
        position = -0.2;
    }
}