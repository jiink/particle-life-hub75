// TODO: Cell optimization https://youtu.be/9IULfQH7E90?t=231

#include <iostream>
#include <raylib.h>
#include <math.h>

#define CANVAS_WIDTH (160) // Resolution of what you want to draw to
#define CANVAS_HEIGHT (120)
#define SCREEN_WIDTH (CANVAS_WIDTH * 4) // How big will it be on your screen?
#define SCREEN_HEIGHT (CANVAS_HEIGHT * 4)

#define MAX_PARTICLES 500
#define MAX_COLOR_GROUPS 2

using namespace std;

//----------------------------------------------------------------------------------
// Local Variables Definition (local to this module)
//----------------------------------------------------------------------------------

static void Initialize(void);
static void UpdateDrawFrame(void); // Update and draw one frame

enum ColorGroup
{
	GROUP_RED,
	GROUP_BLUE
};

const Color ColorGroupColors[] = {
	{255, 20, 67, 255},
	{20, 200, 255, 255}};

const float attractionFactorMatrix[MAX_COLOR_GROUPS][MAX_COLOR_GROUPS] = {
	{1.0, -1.0},
	{0.2, 0.0},
};

struct Particle
{
	Vector2 position;
	Vector2 velocity;
	ColorGroup colorGroup;
	Color colorDraw;
};

Particle particles[MAX_PARTICLES];

//----------------------------------------------------------------------------------
// Main entry point
//----------------------------------------------------------------------------------
int main(void)
{
	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "goofing");

	RenderTexture2D renderTexture = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
	Rectangle source = {0, (float)-CANVAS_HEIGHT, (float)CANVAS_WIDTH, (float)-CANVAS_HEIGHT}; // - Because OpenGL coordinates are inverted
	Rectangle dest = {0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT};

	SetTargetFPS(144);

	Initialize();

	while (!WindowShouldClose()) // Detect window close button or ESC key
	{

		// Draw on the canvas
		BeginTextureMode(renderTexture);
		UpdateDrawFrame();
		EndTextureMode();

		// Draw the canvas to the actual screen (scaling up)
		BeginDrawing();
		ClearBackground(RAYWHITE);
		DrawTexturePro(renderTexture.texture, source, dest, (Vector2){0, 0}, 0.0f, WHITE);
		// DrawFPS(16, 16);
		EndDrawing();
	}

	CloseWindow(); // Close window and OpenGL context

	return 0;
}

// ------------------------------------------------------------
// Implementing Arduino functions
unsigned long millis()
{
	return (unsigned long)(GetTime() * 1000.0);
}
// ------------------------------------------------------------

float RandFloat(float a, float b)
{
	float random = ((float)rand()) / (float)RAND_MAX;
	float diff = b - a;
	float r = random * diff;
	return a + r;
}

uint8_t RandByte(uint8_t a, uint8_t b)
{
	return rand() % (b - a + 1) + a;
}

float SquareIntersectionArea(Vector2 square1, Vector2 square2)
{
	float left = fmax(square1.x, square2.x);
	float right = fmin(square1.x + 1, square2.x + 1);
	float top = fmax(square1.y, square2.y);
	float bottom = fmin(square1.y + 1, square2.y + 1);

	float width = right - left;
	float height = bottom - top;

	if (width <= 0 || height <= 0)
	{
		return 0;
	}
	else
	{
		return width * height;
	}
}

Vector2 Vector2Subtract(Vector2 a, Vector2 b)
{
	return {a.x - b.x, a.y - b.y};
}

float Vector2Length(Vector2 a)
{
	return sqrt(a.x * a.x + a.y * a.y);
}

Vector2 Vector2Multiply(Vector2 a, float scalar)
{
	return {a.x * scalar, a.y * scalar};
}

Vector2 Vector2Add(Vector2 a, Vector2 b)
{
	return {a.x + b.x, a.y + b.y};
}

Vector2 Vector2Normalize(Vector2 vec)
{
	float length = Vector2Length(vec);
	if (length != 0.0f)
	{
		vec.x /= length;
		vec.y /= length;
	}
	return vec;
}

Color MultiplyColor(Color color, float value)
{
	return (Color){
		(uint8_t)(color.r * value),
		(uint8_t)(color.g * value),
		(uint8_t)(color.b * value),
		(uint8_t)(color.a)};
}

// Draws a point on the screen at a sub-pixel level, unlike DrawPixel.
// If the point is in-between screen pixels, it will be rendered using
// its neighboring pixels.
void DrawPoint(Vector2 position, Color color)
{
	// Find the corners of the imaginary pixel-sized square around the point
	Vector2 cornerTopLeft = {position.x - 0.5f, position.y - 0.5f};

	// Find the corners of the squares of the grid pixels around the point
	Vector2 pixelCornerTopLeft = {floorf(position.x - 0.5), floorf(position.y - 0.5)};
	Vector2 pixelCornerTopRight = {pixelCornerTopLeft.x + 1.0, pixelCornerTopLeft.y};
	Vector2 pixelCornerBottomLeft = {pixelCornerTopLeft.x, pixelCornerTopLeft.y + 1.0};
	Vector2 pixelCornerBottomRight = {pixelCornerTopLeft.x + 1.0, pixelCornerTopLeft.y + 1.0};

	// Find the overlapping areas between the imaginary square around the point and
	// the grid squares
	float areaTopLeft = SquareIntersectionArea(cornerTopLeft, pixelCornerTopLeft);
	float areaTopRight = SquareIntersectionArea(cornerTopLeft, pixelCornerTopRight);
	float areaBottomLeft = SquareIntersectionArea(cornerTopLeft, pixelCornerBottomLeft);
	float areaBottomRight = SquareIntersectionArea(cornerTopLeft, pixelCornerBottomRight);

	// Find fractions of color
	Color colorTopLeft = MultiplyColor(color, areaTopLeft);
	Color colorTopRight = MultiplyColor(color, areaTopRight);
	Color colorBottomLeft = MultiplyColor(color, areaBottomLeft);
	Color colorBottomRight = MultiplyColor(color, areaBottomRight);

	// Set pixels
	DrawPixelV(pixelCornerTopLeft, colorTopLeft);
	DrawPixelV(pixelCornerTopRight, colorTopRight);
	DrawPixelV(pixelCornerBottomLeft, colorBottomLeft);
	DrawPixelV(pixelCornerBottomRight, colorBottomRight);
}

// Returns the attraction force that will be applied to the subject
Vector2 GetAttractionForce(Particle pSubject, Particle pObject)
{
	// Get attraction factor from matrix
	uint8_t column = (uint8_t)pSubject.colorGroup;
	uint8_t row = (uint8_t)pObject.colorGroup;
	float attractionFactor = attractionFactorMatrix[row][column];

	Vector2 delta = Vector2Subtract(pSubject.position, pObject.position);
	float distance = Vector2Length(delta);
	const float tooCloseDistance = 7.0f;
	const float tooCloseRepelFactor = 1.0f;
	const float maxDistance = 15.0f + tooCloseDistance;
	float forceMagnitude = 0.0;
	// Repuslive force when nearby
	if (distance < tooCloseDistance)
	{
		forceMagnitude = (((tooCloseRepelFactor * distance) / tooCloseDistance) - tooCloseRepelFactor);
		// if (forceMagnitude > 0.01)
		// {
		// 	printf("Excuse me!\n");
		// }
	}
	else if (distance < tooCloseDistance + maxDistance)
	{ // Ramp up to characteristic value then back down
		forceMagnitude = attractionFactor * (-abs((distance - 0.5 * maxDistance - tooCloseDistance) / (0.5 * maxDistance)) + 1.0);
		// if (forceMagnitude > 0.01)
		// {
		// 	printf("Excuse me!\n");
		// }
	}

	return Vector2Multiply(Vector2Normalize(delta), -forceMagnitude);
}

static void Initialize()
{
	// Initialize the particles with random positions, velocities, and colors
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		particles[i].position = {RandFloat(0, CANVAS_WIDTH), RandFloat(0, CANVAS_HEIGHT)};
		// particles[i].velocity = { RandFloat(-50, 50), RandFloat(-50, 50) };
		// particles[i].velocity = {RandFloat(-1, 1), RandFloat(-1, 1)};
		particles[i].velocity = {0.0, 0.0};

		particles[i].colorGroup = (ColorGroup)RandByte(GROUP_RED, MAX_COLOR_GROUPS - 1);
		
		particles[i].colorDraw = ColorGroupColors[particles[i].colorGroup];
	}
}

static void UpdateDrawFrame()
{
	// Update time
	static unsigned long prevMillis = 0;
	unsigned long currentMillis = millis();
	float deltaTime = (currentMillis - prevMillis) / 1000.0f;
	prevMillis = currentMillis;

	ClearBackground(BLACK);

	// Update and draw each particle
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		for (int j = 0; j < MAX_PARTICLES; j++)
		{
			if (i != j)
			{
				Vector2 force = GetAttractionForce(particles[i], particles[j]);
				particles[i].velocity = Vector2Add(particles[i].velocity, force);
			}
		}

		// Consider forces from other side of wrap, too
		// for (int j = 0; j < MAX_PARTICLES; j++)
		// {
		// 	if (i != j)
		// 	{
		// 		Particle pObject = particles[j];
		// 		pObject.position.x -= CANVAS_WIDTH;
		// 		Vector2 force = GetAttractionForce(particles[i], particles[j]);
		// 		particles[i].velocity = Vector2Add(particles[i].velocity, force);
		// 	}
		// }
		// // Consider forces from other side of wrap, too
		// for (int j = 0; j < MAX_PARTICLES; j++)
		// {
		// 	if (i != j)
		// 	{
		// 		Particle pObject = particles[j];
		// 		pObject.position.x += CANVAS_WIDTH;
		// 		Vector2 force = GetAttractionForce(particles[i], particles[j]);
		// 		particles[i].velocity = Vector2Add(particles[i].velocity, force);
		// 	}
		// }

		// Update the particle's position based on its velocity
		particles[i].position.x += particles[i].velocity.x * deltaTime;
		particles[i].position.y += particles[i].velocity.y * deltaTime;

		// Friction (stupid)
		particles[i].velocity = Vector2Multiply(particles[i].velocity, 0.9);

		// If the particle goes off the screen, wrap it around to the other side
		if (particles[i].position.x < 0)
			particles[i].position.x = CANVAS_WIDTH;
		if (particles[i].position.x > CANVAS_WIDTH)
			particles[i].position.x = 0;
		if (particles[i].position.y < 0)
			particles[i].position.y = CANVAS_HEIGHT;
		if (particles[i].position.y > CANVAS_HEIGHT)
			particles[i].position.y = 0;

		// Draw the particle
		// DrawPixel(particles[i].position.x, particles[i].position.y, particles[i].colorDraw);
		DrawPoint(particles[i].position, particles[i].colorDraw);
	}
}
