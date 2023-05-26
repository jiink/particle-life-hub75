#include "led-matrix.h"
#include "graphics.h"

#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 32

#define CANVAS_WIDTH SCREEN_WIDTH
#define CANVAS_HEIGHT SCREEN_HEIGHT
#define CANVAS_ASPECT_RATIO (CANVAS_WIDTH / CANVAS_HEIGHT)

#define CELL_GRID_WIDTH 4
#define CELL_GRID_HEIGHT 2
#define MAX_PARTICLES_PER_CELL 80

#define MAX_PARTICLES 12
#define MAX_COLOR_GROUPS 2

// Pin defines
#define SW 29   // wPi assignment

using namespace rgb_matrix;

RGBMatrix *matrix;
FrameCanvas *canvas;
Font font;

enum ProgramState
{
    MENU,
    APPLET
};
ProgramState state = MENU;

struct Vector2
{
    float x, y;
};

enum ColorGroup
{
    GROUP_RED,
    GROUP_BLUE,
    GROUP_YELLOW
};

const Color ColorGroupColors[] = {
    {255, 20, 67},
    {20, 200, 255},
    {255, 200, 20},
    {255, 255, 255},
    {20, 255, 180},
};

float attractionFactorMatrix[MAX_COLOR_GROUPS][MAX_COLOR_GROUPS];

struct Particle
{
    Vector2 position;
    Vector2 velocity;
    ColorGroup colorGroup;
};

Particle particles[MAX_PARTICLES];

// In worldspace, the radius of the sphere of influence for each particle.
const float maxDistance = 0.25; // Please let 2 be evenly divisible by this number, for cellSize's sake
const float dt = 0.01;
const float frictionFactor = 0.99;
const float forceFactor = 10.0;


struct Cell
{
    uint16_t particleIndices[MAX_PARTICLES_PER_CELL];
    uint8_t particleCount;
};

// Used to define which way a neighboring cell is wrapped around the edge of the area
struct CellWrap
{
    bool wrappedLeft;
    bool wrappedRight;
    bool wrappedTop;
    bool wrappedBottom;
};

// Divide the area into cells whos size is the
// diameter of the circle of influence for every particle
const float cellSize = maxDistance * 2.0;
// Each grid cell contains a list of particles within its bounds.
Cell grid[CELL_GRID_HEIGHT][CELL_GRID_WIDTH];


volatile bool interrupt_received = false;
static void InterruptHandler(int signo) {
    interrupt_received = true;
}


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
        AddClamp(a.b, b.b)};
}

void InitCellWraps(CellWrap *neighborCellWraps, uint8_t size)
{
    for (uint8_t i = 0; i < size; i++)
    {
        neighborCellWraps[i].wrappedLeft = false;
        neighborCellWraps[i].wrappedRight = false;
        neighborCellWraps[i].wrappedTop = false;
        neighborCellWraps[i].wrappedBottom = false;
    }
}

void GetNeighborCells(Cell **listToPopulate, int row, int col, CellWrap *wrapList)
{
    uint8_t left = (col == 0) ? CELL_GRID_WIDTH - 1 : col - 1;
    uint8_t right = (col + 1) % CELL_GRID_WIDTH;
    uint8_t above = (row == 0) ? CELL_GRID_HEIGHT - 1 : row - 1;
    uint8_t below = (row + 1) % CELL_GRID_HEIGHT;
    /*
    012
    345
    678
    */
    listToPopulate[0] = &grid[above][left];
    listToPopulate[1] = &grid[above][col];
    listToPopulate[2] = &grid[above][right];
    listToPopulate[3] = &grid[row][left];
    listToPopulate[4] = &grid[row][col];
    listToPopulate[5] = &grid[row][right];
    listToPopulate[6] = &grid[below][left];
    listToPopulate[7] = &grid[below][col];
    listToPopulate[8] = &grid[below][right];

    // Which way are the cells wrapped?
    if (col == 0)
    {
        // If the leftmost column, the left neighbors are all wrapped
        wrapList[0].wrappedLeft = true;
        wrapList[3].wrappedLeft = true;
        wrapList[6].wrappedLeft = true;
    }
    else if (col == CELL_GRID_WIDTH - 1)
    {
        // If the rightmost column, the right neighbors are all wrapped
        wrapList[2].wrappedRight = true;
        wrapList[5].wrappedRight = true;
        wrapList[8].wrappedRight = true;
    }
    if (row == 0)
    {
        wrapList[0].wrappedTop = true;
        wrapList[1].wrappedTop = true;
        wrapList[2].wrappedTop = true;
    }
    else if (row == CELL_GRID_HEIGHT - 1)
    {
        wrapList[6].wrappedBottom = true;
        wrapList[7].wrappedBottom = true;
        wrapList[8].wrappedBottom = true;
    }
}


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

Color ColorMultiply(Color color, float value)
{
    return {
        (uint8_t)(color.r * value),
        (uint8_t)(color.g * value),
        (uint8_t)(color.b * value)};
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
    Color colorTopLeft = ColorMultiply(color, areaTopLeft);
    Color colorTopRight = ColorMultiply(color, areaTopRight);
    Color colorBottomLeft = ColorMultiply(color, areaBottomLeft);
    Color colorBottomRight = ColorMultiply(color, areaBottomRight);

    // Set pixels
    // FrameBufferAddPixV(pixelCornerTopLeft, colorTopLeft);
    // FrameBufferAddPixV(pixelCornerTopRight, colorTopRight);
    // FrameBufferAddPixV(pixelCornerBottomLeft, colorBottomLeft);
    // FrameBufferAddPixV(pixelCornerBottomRight, colorBottomRight);
    canvas->SetPixel(pixelCornerTopLeft.x, pixelCornerTopLeft.y, colorTopLeft.r, colorTopLeft.g, colorTopLeft.b);
    canvas->SetPixel(pixelCornerTopRight.x, pixelCornerTopRight.y, colorTopRight.r, colorTopRight.g, colorTopRight.b);
    canvas->SetPixel(pixelCornerBottomLeft.x, pixelCornerBottomLeft.y, colorBottomLeft.r, colorBottomLeft.g, colorBottomLeft.b);
    canvas->SetPixel(pixelCornerBottomRight.x, pixelCornerBottomRight.y, colorBottomRight.r, colorBottomRight.g, colorBottomRight.b);
}

float AttractionForceMag(float distance, float attractionFactor)
{
    // Closer than this, and the particles will push each other away
    const float tooCloseDistance = 0.4;
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


void UpdateGrid()
{
    // Clear the list of particles for each cell
    for (int i = 0; i < CELL_GRID_HEIGHT; i++)
    {
        for (int j = 0; j < CELL_GRID_WIDTH; j++)
        {
            grid[i][j].particleCount = 0;
        }
    }

    // Add each particle to the list of particles for its corresponding cell
    for (int i = 0; i < MAX_PARTICLES; i++)
    {

        int cell_row = (int)(particles[i].position.y / cellSize);
        int cell_col = (int)(particles[i].position.x / cellSize);
        // DebugPrintf("[%d, %d]\n", cell_row, cell_col);
        //  if (cell_row > CELL_GRID_HEIGHT - 1)
        //      continue;
        //  if (cell_col > CELL_GRID_WIDTH - 1)
        //      continue;
        // Cell* cell = &grid[cell_row][cell_col];
        //  if (cell->particleCount < MAX_PARTICLES_PER_CELL)
        //  {
        //      cell->particleIndices[cell->particleCount] = i;
        //      cell->particleCount++;
        //  }
        if (grid[cell_row][cell_col].particleCount < MAX_PARTICLES_PER_CELL)
        {
            grid[cell_row][cell_col].particleIndices[grid[cell_row][cell_col].particleCount] = i;
            grid[cell_row][cell_col].particleCount++;
        }
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

static void initialize()
{
    // Initialize the particles with random positions, velocities, and colors
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
        particles[i].position = {RandFloat(0, CANVAS_ASPECT_RATIO), RandFloat(0, 1)};
        particles[i].velocity = {RandFloat(-10, 10), RandFloat(-10, 10)};
        //particles[i].velocity = {RandFloat(-1, 1), RandFloat(-1, 1)};
        //particles[i].velocity = {0.0, 0.0};

        particles[i].colorGroup = (ColorGroup)RandByte(GROUP_RED, MAX_COLOR_GROUPS - 1);
    }

    // randomizeAttractionFactorMatrix();
    attractionFactorMatrix[0][0] = 1.0;
    attractionFactorMatrix[0][1] = -1.0;
    attractionFactorMatrix[1][0] = 0.2;
    attractionFactorMatrix[1][1] = 0.0;
}

int millis(){
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return (int)(tp.tv_sec * 1000 + tp.tv_usec / 1000);
}

void loop()
{
    // Update time
    static unsigned long prevMillis = 0;
    unsigned long currentMillis = millis();
    float deltaTime = (currentMillis - prevMillis) / 1000.0f;
    prevMillis = currentMillis;

    canvas->Fill(0, 10, 60);

    //canvas->SetPixel(0, 0, 255, 255, 255);

    UpdateGrid();

    // Update each particle, one cell at a time
    for (int r = 0; r < CELL_GRID_HEIGHT; r++)
    {
     for (int c = 0; c < CELL_GRID_WIDTH; c++)
     {
         // Get list of the 8 neighboring cells and itself
         Cell *neighborCells[9];
         CellWrap neighborCellWraps[9];
         InitCellWraps(neighborCellWraps, 9);
         GetNeighborCells(neighborCells, r, c, neighborCellWraps);

         // Go through every particle in this cell (as subjects)
         for (int pI = 0; pI < grid[r][c].particleCount; pI++)
         {
             uint16_t i = grid[r][c].particleIndices[pI];
             // Vector2 totalForce = {0.0, 0.0}; // Will be accumulated when looping through neighbors
             // // Go through each neighboring cell
             // for (int n = 0; n < 9; n++)
             // {
             //  // Go through every particle in this cell (as objects)
             //  for (int pJ = 0; pJ < neighborCells[n]->particleCount; pJ++)
             //  {
             //      uint16_t j = neighborCells[n]->particleIndices[pJ];
             //      if (&particles[j] == &particles[i])
             //          continue;

             //      Vector2 particleObjPercievedPos = particles[j].position;
             //      // Offset location if it's wrapped
             //      if (neighborCellWraps[n].wrappedLeft)
             //      {
             //          // TODO: let these not be constants. Should be aspect ratio (width of field in particle's float coord system)
             //          particleObjPercievedPos.x -= CANVAS_ASPECT_RATIO;
             //      }
             //      if (neighborCellWraps[n].wrappedRight)
             //      {
             //          particleObjPercievedPos.x += CANVAS_ASPECT_RATIO;
             //      }
             //      if (neighborCellWraps[n].wrappedTop)
             //      {
             //          particleObjPercievedPos.y -= 1.0;
             //      }
             //      if (neighborCellWraps[n].wrappedBottom)
             //      {
             //          particleObjPercievedPos.y += 1.0;
             //      }

             //      // Only deal with neighbors within sphere of influence
             //      Vector2 delta = Vector2Subtract(particles[i].position, particleObjPercievedPos);
             //      float distance = Vector2Length(delta);
             //      if (distance > 0.0 && distance < maxDistance)
             //      {
             //          // How hard do I need to move?
             //          float forceMag = AttractionForceMag(distance / maxDistance, attractionFactorMatrix[particles[i].colorGroup][particles[j].colorGroup]);

             //          // Where do I need to move?
             //          // Normalize then scale by force magnitude
             //          Vector2 force = Vector2Scale(delta, -1.0 / distance * forceMag);
             //          totalForce = Vector2Add(totalForce, force);
             //      }
             //  }
             // }

             // totalForce = Vector2Scale(totalForce, maxDistance * forceFactor);

             // particles[i].velocity = Vector2Scale(particles[i].velocity, frictionFactor);
             // particles[i].velocity = Vector2Add(particles[i].velocity, Vector2Scale(totalForce, deltaTime));

             // Update the particle's position based on its velocity
             particles[i].position.x += particles[i].velocity.x * deltaTime;
             particles[i].position.y += particles[i].velocity.y * deltaTime;

             // If the particle goes off the screen, wrap it around to the other side
             if (particles[i].position.x < 0.01)
                 particles[i].position.x = 1.99;
             if (particles[i].position.x > 2)
                 particles[i].position.x = 0.01;
             if (particles[i].position.y < 0.01)
                 particles[i].position.y = 0.99;
             if (particles[i].position.y > 1)
                 particles[i].position.y = 0.01;
         }
     }
    }

    // Draw each particle
    for (int i = 0; i < MAX_PARTICLES; i++)
    {
     // Scale from world space to screen space
     Vector2 posOnScreen = {particles[i].position.x * CANVAS_WIDTH / (CANVAS_ASPECT_RATIO), particles[i].position.y * CANVAS_HEIGHT};
     DrawPoint(posOnScreen, ColorGroupColors[particles[i].colorGroup]);
     // char report[64];
     // sprintf(report, "%d: %d, %d; ",  i, (int)posOnScreen.x, (int)posOnScreen.y);
     // Serial.println(report);
     //Color color = ColorGroupColors[particles[i].colorGroup];
     //canvas->SetPixel(posOnScreen.x, posOnScreen.y, color.r, color.g, color.b);
    }

   
    canvas = matrix->SwapOnVSync(canvas);
}

int main(int argc, char *argv[]) {
    RGBMatrix::Options matrix_options;
    RuntimeOptions runtime_opt;
    if (!ParseOptionsFromFlags(&argc, &argv,
                                         &matrix_options, &runtime_opt)) {
        return 1;
    }
  
    matrix = RGBMatrix::CreateFromOptions(matrix_options, runtime_opt);
    if (matrix == NULL)
    {
        return 1;
    }
    canvas = matrix->CreateFrameCanvas();


    // It is always good to set up a signal handler to cleanly exit when we
    // receive a CTRL-C for instance. The DrawOnCanvas() routine is looking
    // for that.
    signal(SIGTERM, InterruptHandler);
    signal(SIGINT, InterruptHandler);

    initialize();
    while (!interrupt_received)
    {
        loop();
    }

    delete matrix;

    return 0;
}
