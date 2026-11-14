//========================================================================
// Input event timestamp injection test
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
// This test injects synthetic input events with controlled timestamps via
// the internal GLFW event time interface and measures the timestamps
// reported by glfwGetInputEventTime
//
// Each scenario runs in a fresh process because the event clock
// calibration is process-global state
//
// Scenarios:
//   latency    fallback path (no native timestamp) processing latency
//   accuracy   native timestamp conversion accuracy with 5 ms event lag
//   monotonic  out-of-order native timestamps never move reported time back
//   wrap       32-bit millisecond counter wraparound is widened correctly
//   future     future timestamps are clamped to the current time
//   absolute   absolute (timer domain) timestamps, including future clamp
//   windows    two windows share one time domain without jumps
//
//========================================================================

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <time.h>
#endif

// Internal GLFW functions used to inject synthetic events
// These are declared here to avoid pulling in the internal headers
extern void _glfwSetEventTime(uint32_t time);
extern void _glfwSetEventTimeAbsolute(uint64_t time);
extern void _glfwInputKey(GLFWwindow* window,
                          int key, int scancode, int action, int mods);
extern void _glfwInputMouseClick(GLFWwindow* window,
                                 int button, int action, int mods);

static int failures;

#define CHECK(condition, ...) \
    do { \
        if (!(condition)) { \
            failures++; \
            printf("FAIL: "); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while (0)

static uint32_t now_ms(void)
{
    return (uint32_t) (glfwGetTimerValue() * 1000 /
                       glfwGetTimerFrequency());
}

static void sleep_ms(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD) ms);
#else
    struct timespec duration = { ms / 1000, (ms % 1000) * 1000000 };
    nanosleep(&duration, NULL);
#endif
}

static void inject_key(GLFWwindow* window)
{
    static int action = GLFW_PRESS;
    _glfwInputKey(window, GLFW_KEY_A, 30, action, 0);
    action = (action == GLFW_PRESS) ? GLFW_RELEASE : GLFW_PRESS;
}

static int scenario_latency(GLFWwindow* window)
{
    const int count = 10000;
    double worst = 0.0, total = 0.0;

    // No call to _glfwSetEventTime, so events are stamped at dispatch time
    for (int i = 0; i < count; i++)
    {
        inject_key(window);

        const double latency = glfwGetTime() - glfwGetInputEventTime(window);
        if (latency > worst)
            worst = latency;
        total += latency;
    }

    printf("latency:    fallback dispatch stamping over %d events: "
           "mean %.3f us, worst %.3f us\n",
           count, total / count * 1e6, worst * 1e6);

    CHECK(worst < 0.001, "fallback latency above 1 ms: %.3f us", worst * 1e6);
    return 0;
}

static int scenario_accuracy(GLFWwindow* window)
{
    const int count = 1000;
    const double lag = 0.005;
    double worst = 0.0, total = 0.0;

    // Each event is stamped 5 ms in the past, simulating the time between
    // the hardware event and its dispatch by the application
    for (int i = 0; i < count; i++)
    {
        _glfwSetEventTime(now_ms() - 5);
        inject_key(window);

        const double expected = glfwGetTime() - lag;
        const double error = glfwGetInputEventTime(window) - expected;
        if (fabs(error) > worst)
            worst = fabs(error);
        total += error;
    }

    printf("accuracy:   native timestamp conversion over %d events: "
           "mean error %+.3f ms, worst %.3f ms\n",
           count, total / count * 1e3, worst * 1e3);

    CHECK(worst <= 0.0015,
          "conversion error above 1.5 ms: %.3f ms", worst * 1e3);
    return 0;
}

static int scenario_monotonic(GLFWwindow* window)
{
    const int count = 10000;
    double last = 0.0;
    int have_last = 0;
    int violations = 0;

    // Native timestamps arrive out of order; reported times must not
    for (int i = 0; i < count; i++)
    {
        uint32_t time = now_ms();
        if (i % 3 == 0)
            time -= 50;

        _glfwSetEventTime(time);
        inject_key(window);

        const double reported = glfwGetInputEventTime(window);
        if (have_last && reported < last)
            violations++;
        last = reported;
        have_last = 1;
    }

    printf("monotonic:  out-of-order timestamps over %d events: "
           "%d monotonicity violations\n",
           count, violations);

    CHECK(violations == 0, "%d monotonicity violations", violations);
    return 0;
}

static int scenario_wrap(GLFWwindow* window)
{
    const int count = 40;
    const uint32_t start = 0xfffffff0u;
    double last = 0.0;
    int violations = 0, jumps = 0;

    // Step a 32-bit millisecond counter across its wraparound point,
    // sleeping so each step stays in the past relative to the timer
    for (int i = 0; i < count; i++)
    {
        _glfwSetEventTime(start + (uint32_t) i);
        inject_key(window);

        const double reported = glfwGetInputEventTime(window);
        if (i > 0)
        {
            const double step = reported - last;
            if (step < 0.0)
                violations++;
            if (step < 0.0005 || step > 0.010)
                jumps++;
        }
        last = reported;

        sleep_ms(2);
    }

    printf("wrap:       32-bit counter wraparound over %d events: "
           "%d violations, %d irregular steps\n",
           count, violations, jumps);

    CHECK(violations == 0, "%d monotonicity violations across wrap", violations);
    CHECK(jumps == 0, "%d irregular steps across wrap", jumps);
    return 0;
}

static int scenario_future(GLFWwindow* window)
{
    // A timestamp one second in the future must be clamped to now
    _glfwSetEventTime(now_ms() + 1000);
    inject_key(window);

    const double error = glfwGetInputEventTime(window) - glfwGetTime();
    printf("future:     timestamp 1 s in the future clamped, "
           "reported %.3f ms from now\n",
           error * 1e3);

    CHECK(error <= 0.0 && error > -0.001,
          "future timestamp not clamped to now: %.3f ms", error * 1e3);

    // A regular event afterwards must still be reported correctly
    sleep_ms(2);
    _glfwSetEventTime(now_ms());
    inject_key(window);

    const double latency = glfwGetTime() - glfwGetInputEventTime(window);
    CHECK(latency >= 0.0 && latency < 0.010,
          "event after recalibration off by %.3f ms", latency * 1e3);
    return 0;
}

static int scenario_absolute(GLFWwindow* window)
{
    const uint64_t frequency = glfwGetTimerFrequency();

    // Make sure enough time has passed since initialization for the
    // timestamp below to stay positive
    sleep_ms(10);

    // An absolute timestamp 3 ms in the past
    _glfwSetEventTimeAbsolute(glfwGetTimerValue() - frequency * 3 / 1000);
    inject_key(window);

    const double lag = glfwGetTime() - glfwGetInputEventTime(window);
    printf("absolute:   absolute timestamp 3 ms in the past, "
           "reported lag %.3f ms\n",
           lag * 1e3);

    CHECK(lag > 0.002 && lag < 0.004,
          "absolute timestamp off by %.3f ms", lag * 1e3);

    // An absolute timestamp in the future must be clamped to now
    _glfwSetEventTimeAbsolute(glfwGetTimerValue() + frequency);
    inject_key(window);

    const double error = glfwGetInputEventTime(window) - glfwGetTime();
    CHECK(error <= 0.0 && error > -0.001,
          "future absolute timestamp not clamped: %.3f ms", error * 1e3);
    return 0;
}

static int scenario_windows(GLFWwindow* window)
{
    GLFWwindow* other = glfwCreateWindow(64, 64, "other", NULL, NULL);
    if (!other)
    {
        printf("FAIL: could not create second window\n");
        failures++;
        return 0;
    }

    const int count = 100;
    double last[2] = { 0.0, 0.0 };
    int seen[2] = { 0, 0 };
    int violations = 0;

    // Alternate events between the two windows; both must report times in
    // the same shared, monotonically increasing domain
    for (int i = 0; i < count; i++)
    {
        GLFWwindow* target = (i % 2) ? other : window;

        _glfwSetEventTime(now_ms());
        _glfwInputMouseClick(target, GLFW_MOUSE_BUTTON_LEFT,
                             (i % 4) < 2 ? GLFW_PRESS : GLFW_RELEASE, 0);

        const double reported = glfwGetInputEventTime(target);
        if (seen[i % 2] && reported < last[i % 2])
            violations++;
        last[i % 2] = reported;
        seen[i % 2] = 1;

        sleep_ms(1);
    }

    // The most recent event belongs to one window; the other window must
    // report a time close to it, proving a shared time domain
    const double gap = fabs(last[0] - last[1]);
    printf("windows:    alternating events across two windows: "
           "%d violations, inter-window gap %.3f ms\n",
           violations, gap * 1e3);

    CHECK(violations == 0, "%d monotonicity violations", violations);
    CHECK(gap < 0.010, "inter-window time gap %.3f ms", gap * 1e3);

    glfwDestroyWindow(other);
    return 0;
}

static void error_callback(int error, const char* description)
{
    fprintf(stderr, "Error: %s\n", description);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr,
                "Usage: %s latency|accuracy|monotonic|wrap|future|"
                "absolute|windows\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    glfwSetErrorCallback(error_callback);

    // Synthetic events are injected directly, so no real platform is needed
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);

    if (!glfwInit())
        return EXIT_FAILURE;

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(64, 64, "inject", NULL, NULL);
    if (!window)
    {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "latency") == 0)
        scenario_latency(window);
    else if (strcmp(argv[1], "accuracy") == 0)
        scenario_accuracy(window);
    else if (strcmp(argv[1], "monotonic") == 0)
        scenario_monotonic(window);
    else if (strcmp(argv[1], "wrap") == 0)
        scenario_wrap(window);
    else if (strcmp(argv[1], "future") == 0)
        scenario_future(window);
    else if (strcmp(argv[1], "absolute") == 0)
        scenario_absolute(window);
    else if (strcmp(argv[1], "windows") == 0)
        scenario_windows(window);
    else
    {
        fprintf(stderr, "Unknown scenario: %s\n", argv[1]);
        failures++;
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    if (failures)
        printf("result:   %d check(s) FAILED\n", failures);
    else
        printf("result:   all checks passed\n");

    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
