//========================================================================
// Window resize callback order test
//========================================================================
//
// This test drives window resizes from the framebuffer size callback to check
// that a resize callback can safely trigger the next resize and that events are
// still delivered in occurrence order without losing the final size.
//
//========================================================================

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>

#define RESIZE_COUNT 100

typedef struct Size
{
    int width;
    int height;
} Size;

typedef struct Event
{
    char type;
    Size size;
} Event;

static GLFWwindow* window;
static Size requested[RESIZE_COUNT];
static Event events[RESIZE_COUNT * 2];
static int asked;
static int received;
static int overflowed;

static void framebufferSizeCallback(GLFWwindow* handle, int width, int height)
{
    if (received < RESIZE_COUNT * 2)
        events[received++] = (Event) { 'F', { width, height } };
    else
        overflowed = 1;

    if (asked < RESIZE_COUNT)
    {
        const Size next = requested[asked++];
        glfwSetWindowSize(handle, next.width, next.height);
    }
}

static void windowSizeCallback(GLFWwindow* handle, int width, int height)
{
    (void) handle;

    if (received < RESIZE_COUNT * 2)
        events[received++] = (Event) { 'W', { width, height } };
    else
        overflowed = 1;
}

static unsigned int nextRandom(unsigned int* state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

int main(void)
{
    int i;
    int failures = 0;
    int orderedPairs = 0;
    int reportedMismatch = 0;
    unsigned int random = 0x5eed1234u;

    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(16, 16, "resize callback order", NULL, NULL);
    if (!window)
        return EXIT_FAILURE;

    for (i = 0; i < RESIZE_COUNT; i++)
    {
        requested[i].width = 32 + (int) (nextRandom(&random) % 1024u);
        requested[i].height = 32 + (int) (nextRandom(&random) % 1024u);

        if (i > 0 &&
            requested[i].width == requested[i - 1].width &&
            requested[i].height == requested[i - 1].height)
        {
            requested[i].width++;
        }
    }

    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetWindowSizeCallback(window, windowSizeCallback);

    asked = 1;
    glfwSetWindowSize(window, requested[0].width, requested[0].height);

    if (received != RESIZE_COUNT * 2 || overflowed)
    {
        fprintf(stderr, "Expected %d callbacks, received %d%s\n",
                RESIZE_COUNT * 2,
                received,
                overflowed ? " (overflowed event buffer)" : "");
        failures++;
    }

    for (i = 0; i < RESIZE_COUNT && i * 2 + 1 < received; i++)
    {
        const Event* fb = &events[i * 2];
        const Event* size = &events[i * 2 + 1];

        if (fb->type != 'F' ||
            fb->size.width != requested[i].width ||
            fb->size.height != requested[i].height ||
            size->type != 'W' ||
            size->size.width != requested[i].width ||
            size->size.height != requested[i].height)
        {
            if (!reportedMismatch)
            {
                fprintf(stderr,
                        "Mismatch for resize %d: %c(%dx%d) %c(%dx%d), expected F(%dx%d) W(%dx%d)\n",
                        i,
                        fb->type, fb->size.width, fb->size.height,
                        size->type, size->size.width, size->size.height,
                        requested[i].width, requested[i].height,
                        requested[i].width, requested[i].height);
                reportedMismatch = 1;
            }

            failures++;
        }
        else
        {
            orderedPairs++;
        }
    }

    {
        int width, height;
        glfwGetWindowSize(window, &width, &height);

        if (width != requested[RESIZE_COUNT - 1].width ||
            height != requested[RESIZE_COUNT - 1].height)
        {
            fprintf(stderr, "Final size is %dx%d, expected %dx%d\n",
                    width, height,
                    requested[RESIZE_COUNT - 1].width,
                    requested[RESIZE_COUNT - 1].height);
            failures++;
        }

        printf("Callback order (first 4 pairs):\n");
        for (i = 0; i < 8 && i < received; i++)
        {
            printf("%s%c(%dx%d)",
                   i ? " -> " : "  ",
                   events[i].type,
                   events[i].size.width,
                   events[i].size.height);
        }
        printf("\n");

        printf("Callback order (last 2 pairs):\n");
        for (i = received - 4; i < received; i++)
        {
            printf("%s%c(%dx%d)",
                   i > received - 4 ? " -> " : "  ",
                   events[i].type,
                   events[i].size.width,
                   events[i].size.height);
        }
        printf("\n");

        printf("Requests: %d, callbacks: %d, ordered pairs: %d/%d\n",
               RESIZE_COUNT,
               received,
               orderedPairs,
               RESIZE_COUNT);
        if (received)
        {
            printf("Last callback: %c(%dx%d), final query: %dx%d, result: %s\n",
                   events[received - 1].type,
                   events[received - 1].size.width,
                   events[received - 1].size.height,
                   width,
                   height,
                   failures ? "FAIL" : "PASS");
        }
        else
        {
            printf("Last callback: none, final query: %dx%d, result: FAIL\n",
                   width,
                   height);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
