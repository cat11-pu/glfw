//========================================================================
// Input event timestamp test
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
//
// This test prints the timestamp of each input event along with the
// current time, the latency between the event and its processing and the
// time of the current frame, to verify that input event timestamps are
// monotonic and aligned with frame times
//
//========================================================================

#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>

static double frame_time;
static double last_event_time;
static unsigned int monotonic_violations;

static void check_event_time(GLFWwindow* window, const char* name)
{
    const double event_time = glfwGetInputEventTime(window);
    const double now = glfwGetTime();

    if (event_time < last_event_time)
    {
        monotonic_violations++;
        printf("MONOTONICITY VIOLATION: %s event time %.6f < previous %.6f\n",
               name, event_time, last_event_time);
    }

    last_event_time = event_time;

    printf("%-20s event %10.6f  now %10.6f  latency %7.3f ms  frame %+8.3f ms\n",
           name,
           event_time,
           now,
           (now - event_time) * 1000.0,
           (event_time - frame_time) * 1000.0);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    check_event_time(window, "key");

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    check_event_time(window, "mouse button");
}

static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos)
{
    check_event_time(window, "cursor pos");
}

static void cursor_enter_callback(GLFWwindow* window, int entered)
{
    check_event_time(window, "cursor enter");
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    check_event_time(window, "scroll");
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

int main(void)
{
    glfwSetErrorCallback(error_callback);

    if (!glfwInit())
        exit(EXIT_FAILURE);

    GLFWwindow* window = glfwCreateWindow(640, 480, "Event Time", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }

    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetCursorEnterCallback(window, cursor_enter_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glfwMakeContextCurrent(window);
    gladLoadGL(glfwGetProcAddress);
    glfwSwapInterval(1);

    printf("Generate input events; each line shows the event timestamp,\n");
    printf("processing latency and offset from the last frame swap\n");

    while (!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
        frame_time = glfwGetTime();

        glfwPollEvents();
    }

    printf("Monotonicity violations: %u\n", monotonic_violations);

    glfwDestroyWindow(window);
    glfwTerminate();

    exit(monotonic_violations ? EXIT_FAILURE : EXIT_SUCCESS);
}
