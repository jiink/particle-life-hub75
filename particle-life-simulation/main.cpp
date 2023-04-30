// TODO: Cell optimization https://youtu.be/9IULfQH7E90?t=231
// Resources:
// 		https://youtu.be/scvuli-zcRc
//		https://www.reddit.com/r/raylib/comments/hcglzh/c_reasonable_performance_pixelbypixel_display/g212jbl/

#include <iostream>
#include <raylib.h>
#include <math.h>

#define CANVAS_WIDTH (64 * 1) // Resolution of what you want to draw to
#define CANVAS_HEIGHT (32 * 1)
#define CANVAS_ASPECT_RATIO (CANVAS_WIDTH / CANVAS_HEIGHT)
#define SCREEN_WIDTH (CANVAS_WIDTH * 6) // How big will it be on your screen?
#define SCREEN_HEIGHT (CANVAS_HEIGHT * 6)

#define MAX_PARTICLES 250
#define MAX_COLOR_GROUPS 2

using namespace std;

//----------------------------------------------------------------------------------
// Local Variables Definition (local to this module)
//----------------------------------------------------------------------------------

static void Initialize(void);
static void UpdateDrawFrame(void); // Update and draw one frame

// Set and get pixels from here
Color FrameBuffer[CANVAS_HEIGHT][CANVAS_WIDTH];

// Image canvasImage = { .data = FrameBuffer, .width = CANVAS_WIDTH, .height = CANVAS_HEIGHT, .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };

enum ColorGroup
{
	GROUP_RED,
	GROUP_BLUE,
	GROUP_YELLOW
};

const Color ColorGroupColors[] = {
	{255, 20, 67, 255},
	{20, 200, 255, 255},
	{255, 200, 20, 255}};

float attractionFactorMatrix[MAX_COLOR_GROUPS][MAX_COLOR_GROUPS];

struct Particle
{
	Vector2 position;
	Vector2 velocity;
	ColorGroup colorGroup;
};

Particle particles[MAX_PARTICLES];

// In worldspace, the radius of the sphere of influence for each particle.
const float maxDistance = 0.3;
const float frictionHalfLife = 0.04;
const float dt = 0.01;
const float frictionFactor = pow(0.5, dt / frictionHalfLife);
const float forceFactor = 5.0;

// If there is an overflow, return 255
uint8_t AddClamp(uint8_t a, uint8_t b)
{
	uint8_t sum = a + b;
	if (sum < a || sum < b)
	{
		return 255;
	}
	else
	{
		return sum;
	}
}

Color ColorAdd(Color a, Color b)
{
	return {
		AddClamp(a.r, b.r),
		AddClamp(a.g, b.g),
		AddClamp(a.b, b.b),
		AddClamp(a.a, b.a)};
}

void FrameBufferClear(Color color)
{
	for (int y = 0; y < CANVAS_HEIGHT; y++)
	{
		for (int x = 0; x < CANVAS_WIDTH; x++)
		{
			FrameBuffer[y][x] = color;
		}
	}
}

void FrameBufferSetPix(int x, int y, Color color)
{
	if (x < 0 || y < 0)
		return;
	if (x > CANVAS_WIDTH - 1 || y > CANVAS_HEIGHT - 1)
		return;
	FrameBuffer[y][x] = color;
}

void FrameBufferSetPixV(Vector2 pos, Color color)
{
	FrameBufferSetPix(pos.x, pos.y, color);
}

Color FrameBufferGetPix(int x, int y)
{
	return FrameBuffer[y][x];
}

Color FrameBufferGetPixV(Vector2 pos)
{
	return FrameBufferGetPix(pos.x, pos.y);
}

void FrameBufferAddPix(int x, int y, Color color)
{
	if (x < 0 || y < 0)
		return;
	if (x > CANVAS_WIDTH - 1 || y > CANVAS_HEIGHT - 1)
		return;
	FrameBufferSetPix(x, y, ColorAdd(FrameBufferGetPix(x, y), color));
}

void FrameBufferAddPixV(Vector2 pos, Color color)
{
	FrameBufferAddPix(pos.x, pos.y, color);
}


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
		// Transfer colors from array to rendertexture
		for (int y = 0; y < CANVAS_HEIGHT; y++)
		{
			for (int x = 0; x < CANVAS_WIDTH; x++)
			{
				DrawPixel(x, y, FrameBufferGetPix(x, y));
			}
		}
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

Vector2 Vector2Scale(Vector2 a, float scalar)
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
	// DrawPixelV(pixelCornerTopLeft, colorTopLeft);
	// DrawPixelV(pixelCornerTopRight, colorTopRight);
	// DrawPixelV(pixelCornerBottomLeft, colorBottomLeft);
	// DrawPixelV(pixelCornerBottomRight, colorBottomRight);
	FrameBufferAddPixV(pixelCornerTopLeft, colorTopLeft);
	FrameBufferAddPixV(pixelCornerTopRight, colorTopRight);
	FrameBufferAddPixV(pixelCornerBottomLeft, colorBottomLeft);
	FrameBufferAddPixV(pixelCornerBottomRight, colorBottomRight);
}

float AttractionForceMag(float distance, float attractionFactor)
{
	// Closer than this, and the particles will push each other away
	const float tooCloseDistance = 0.1;
	if (distance < tooCloseDistance)
	{
		// Get away from me!
		return distance / tooCloseDistance - 1;
	}
	else if (tooCloseDistance < distance && distance < 1)
	{
		// Come closer
		return attractionFactor * (1.0 - abs(2.0 * distance - 1 - tooCloseDistance) / (1 - tooCloseDistance));
	}
	else
	{
		return 0.0;
	}
}

void randomizeAttractionFactorMatrix()
{
	for (int i = 0; i < MAX_COLOR_GROUPS; i++)
	{
		for (int j = 0; j < MAX_COLOR_GROUPS; j++)
		{
			attractionFactorMatrix[j][i] = RandFloat(-1.0, 1.0);
		}
	}
}

static void Initialize()
{
	// Initialize the particles with random positions, velocities, and colors
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		particles[i].position = {RandFloat(0, CANVAS_ASPECT_RATIO), RandFloat(0, 1)};
		// particles[i].velocity = { RandFloat(-50, 50), RandFloat(-50, 50) };
		// particles[i].velocity = {RandFloat(-1, 1), RandFloat(-1, 1)};
		particles[i].velocity = {0.0, 0.0};

		particles[i].colorGroup = (ColorGroup)RandByte(GROUP_RED, MAX_COLOR_GROUPS - 1);

		// particles[i].colorDraw = ColorGroupColors[particles[i].colorGroup];
	}

	//randomizeAttractionFactorMatrix();
	attractionFactorMatrix[0][0] = 1.0;
	attractionFactorMatrix[0][1] = -1.0;
	attractionFactorMatrix[1][0] = 0.4;
	attractionFactorMatrix[1][1] = 0.0;
}

static void UpdateDrawFrame()
{
	// Update time
	static unsigned long prevMillis = 0;
	unsigned long currentMillis = millis();
	float deltaTime = (currentMillis - prevMillis) / 1000.0f;
	prevMillis = currentMillis;

	FrameBufferClear({0, 0, 0, 255});

	// Update each particle
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		Vector2 totalForce = {0.0, 0.0}; // Will be accumulated when looping through neighbors
		for (int j = 0; j < MAX_PARTICLES; j++)
		{
			if (j == i)
				continue;
			// Only deal with neighbors within sphere of influence
			Vector2 delta = Vector2Subtract(particles[i].position, particles[j].position);
			float distance = Vector2Length(delta);
			if (distance > 0.0 && distance < maxDistance)
			{
				// How hard do I need to move?
				float forceMag = AttractionForceMag(distance / maxDistance, attractionFactorMatrix[particles[i].colorGroup][particles[j].colorGroup]);

				// Where do I need to move?
				// Normalize then scale by force magnitude
				Vector2 force = Vector2Scale(delta, -1.0 / distance * forceMag);
				totalForce = Vector2Add(totalForce, force);
			}
		}

		totalForce = Vector2Scale(totalForce, maxDistance * forceFactor);

		particles[i].velocity = Vector2Scale(particles[i].velocity, frictionFactor);
		particles[i].velocity = Vector2Add(particles[i].velocity, Vector2Scale(totalForce, deltaTime));

		// Update the particle's position based on its velocity
		particles[i].position.x += particles[i].velocity.x * deltaTime;
		particles[i].position.y += particles[i].velocity.y * deltaTime;

		// If the particle goes off the screen, wrap it around to the other side
		if (particles[i].position.x < 0)
			particles[i].position.x = CANVAS_ASPECT_RATIO;
		if (particles[i].position.x > CANVAS_ASPECT_RATIO)
			particles[i].position.x = 0;
		if (particles[i].position.y < 0)
			particles[i].position.y = 1;
		if (particles[i].position.y > 1)
			particles[i].position.y = 0;
	}

	// Draw each particle
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		// Scale from world space to screen space
		Vector2 posOnScreen = {particles[i].position.x * CANVAS_WIDTH / (CANVAS_ASPECT_RATIO), particles[i].position.y * CANVAS_HEIGHT};
		DrawPoint(posOnScreen, ColorGroupColors[particles[i].colorGroup]);
		// FrameBufferSetPixV(posOnScreen, ColorGroupColors[particles[i].colorGroup]);
	}
}
