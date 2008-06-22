#ifndef Renderable_HPP
#define Renderable_HPP

#include <string.h>
#include "TextureManager.hpp"

class RenderItem
{
public:
	int texsize;
	float aspectRatio;
	bool aspectCorrect;

	virtual void Draw() = 0;
};

class TexturedItem
{
public:
	TextureManager *textureManager;
};

class Shape : public TexturedItem, public RenderItem
{
public:
    std::string imageUrl;
    int sides;
    bool thickOutline;
    bool enabled;
    bool additive;
    bool textured;

    float tex_zoom;
    float tex_ang;

    float x; /* x position  */
    float y; /* y position  */
    float radius;
    float ang;

    float r; /* red color value */
    float g; /* green color value */
    float b; /* blue color value */
    float a; /* alpha color value */

    float r2; /* red color value */
    float g2; /* green color value */
    float b2; /* blue color value */
    float a2; /* alpha color value */

    float border_r; /* red color value */
    float border_g; /* green color value */
    float border_b; /* blue color value */
    float border_a; /* alpha color value */

    Shape();
    void Draw();
};

class Text : RenderItem
{
};

class MotionVectors : public RenderItem
{
public:
    float r;
    float g;
    float b;
    float a;
    float length;
    float x_num;
    float y_num;
    float x_offset;
    float y_offset;

    void Draw();
    MotionVectors(){}
};

class Border : public RenderItem
{
public:
    float outer_size;
    float outer_r;
    float outer_g;
    float outer_b;
    float outer_a;

    float inner_size;
    float inner_r;
    float inner_g;
    float inner_b;
    float inner_a;

    void Draw();
    Border(){}
};
#endif