#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SEQUENCES 1000
#define DEFAULT_SEED UINT64_C(11400714819323198485)
#define GROUPS_PER_SEQUENCE 8
#define MAX_MONITORS 64
#define MAX_LAYOUTS 8
#define MAX_EVENTS 64
#define MAX_FAILURE_REPORTS 20

typedef struct
{
    int x;
    int y;
    int width;
    int height;
} Rect;

typedef struct
{
    const char* name;
    int monitorCount;
    Rect monitors[MAX_MONITORS];
} MonitorLayout;

enum EventType
{
    EVENT_POS,
    EVENT_SIZE,
    EVENT_FRAMEBUFFER_SIZE,
    EVENT_ICONIFY,
    EVENT_MAXIMIZE,
    EVENT_REFRESH,
    EVENT_FOCUS,
    EVENT_TYPE_COUNT
};

typedef struct
{
    int type;
    int first;
    int second;
} EventRecord;

typedef struct
{
    GLFWwindow* handle;
    EventRecord events[MAX_EVENTS];
    int eventCount;
} TestWindow;

typedef struct
{
    int xpos;
    int ypos;
    int width;
    int height;
    int framebufferWidth;
    int framebufferHeight;
    int iconified;
    int maximized;
    int visible;
    int focused;
} Snapshot;

typedef struct
{
    TestWindow test;
    int xpos;
    int ypos;
    int width;
    int height;
    int iconified;
    int maximized;
    int visible;
    int focused;
} WindowModel;

typedef struct
{
    uint64_t state;
} Random;

typedef struct
{
    uint64_t seed;
    int sequencesRequested;
    int sequencesCompleted;
    int operationGroups;
    int operationsAttempted;
    int operations[4];
    int zeroSize[3];
    int zeroSizeTotal;
    int windowsCreated;
    int windowsDestroyed;
    int stateSnapshots;
    int eventGroups;
    int expectedEvents[EVENT_TYPE_COUNT];
    int observedEvents[EVENT_TYPE_COUNT];
    int layoutOperations[MAX_LAYOUTS];
    int layoutMoves[MAX_LAYOUTS];
    int monitorTouches[MAX_LAYOUTS][MAX_MONITORS];
    int failures;
    int failureReports;
    int glfwErrors;
    int lastGlfwError;
    int actualMonitorCount;
    double elapsedSeconds;
    const char* jsonPath;
    int verbose;
} TestContext;

static TestContext context;
static MonitorLayout layouts[MAX_LAYOUTS];

static const char* const eventNames[EVENT_TYPE_COUNT] =
{
    "position",
    "size",
    "framebuffer_size",
    "iconify",
    "maximize",
    "refresh",
    "focus"
};

static const char* const operationNames[4] =
{
    "resize",
    "iconify",
    "restore",
    "move"
};

static void reportFailure(const char* format, ...)
{
    va_list arguments;

    context.failures++;
    if (context.failureReports >= MAX_FAILURE_REPORTS)
        return;

    context.failureReports++;
    fprintf(stderr, "FAIL: ");

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);

    fputc('\n', stderr);
}

static void reportError(int error, const char* description)
{
    context.glfwErrors++;
    context.lastGlfwError = error;
    reportFailure("GLFW error 0x%08X: %s", error, description ? description : "(no description)");
}

static uint64_t randomNext(Random* random)
{
    uint64_t value = random->state;
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    random->state = value;
    return value * UINT64_C(2685821657736338717);
}

static int randomBelow(Random* random, int limit)
{
    if (limit <= 1)
        return 0;

    return (int) (randomNext(random) % (uint64_t) limit);
}

static void addLayout(const char* name, int monitorCount, int columns,
                      int width, int height, int horizontalGap, int verticalGap,
                      int originX, int originY)
{
    MonitorLayout* layout;
    int index;

    layout = &layouts[0];
    for (index = 0;  index < MAX_LAYOUTS;  index++)
    {
        if (layouts[index].name == NULL)
        {
            layout = &layouts[index];
            break;
        }
    }

    layout->name = name;
    layout->monitorCount = monitorCount;

    for (index = 0;  index < monitorCount;  index++)
    {
        const int column = index % columns;
        const int row = index / columns;

        layout->monitors[index].x = originX + column * (width + horizontalGap);
        layout->monitors[index].y = originY + row * (height + verticalGap);
        layout->monitors[index].width = width;
        layout->monitors[index].height = height;
    }
}

static void makeStaggeredLayout(void)
{
    MonitorLayout* layout = &layouts[7];
    int index;

    layout->name = "64-monitor-staggered";
    layout->monitorCount = MAX_MONITORS;

    for (index = 0;  index < MAX_MONITORS;  index++)
    {
        const int column = index % 8;
        const int row = index / 8;
        Rect* monitor = &layout->monitors[index];

        monitor->width = 1280 + (index % 4) * 320;
        monitor->height = 720 + (index % 3) * 180;
        monitor->x = column * 1600 + (row % 2) * 96 - 480;
        monitor->y = row * 900 + (column % 2) * 64 - 480;
    }
}

static void initializeLayouts(void)
{
    memset(layouts, 0, sizeof(layouts));

    addLayout("1-monitor-1080p", 1, 1, 1920, 1080, 0, 0, 0, 0);
    addLayout("2-monitor-row", 2, 2, 1920, 1080, 0, 0, -1920, 0);
    addLayout("4-monitor-grid", 4, 2, 1920, 1080, 16, 24, -1936, -1104);
    addLayout("8-monitor-row", 8, 8, 1920, 1080, 0, 0, -7680, 0);
    addLayout("16-monitor-grid", 16, 4, 1920, 1080, 8, 8, -2892, -2176);
    addLayout("32-monitor-grid", 32, 8, 1920, 1080, 8, 8, -7728, -4360);
    addLayout("64-monitor-grid", MAX_MONITORS, 8, 1920, 1080, 4, 4, -7714, -4348);
    makeStaggeredLayout();
}

static void recordEvent(TestWindow* window, int type, int first, int second)
{
    if (window->eventCount < MAX_EVENTS)
    {
        EventRecord* event = &window->events[window->eventCount];
        event->type = type;
        event->first = first;
        event->second = second;
    }
    else
    {
        reportFailure("Event buffer overflow for window %p", (void*) window->handle);
    }

    window->eventCount++;
    context.observedEvents[type]++;
}

static TestWindow* getTestWindow(GLFWwindow* handle)
{
    WindowModel* model = glfwGetWindowUserPointer(handle);
    return model ? &model->test : NULL;
}

static WindowModel* getWindowModel(GLFWwindow* handle)
{
    return glfwGetWindowUserPointer(handle);
}

static void sizeCallback(GLFWwindow* handle, int width, int height)
{
    int actualWidth, actualHeight;
    TestWindow* window = getTestWindow(handle);

    glfwGetFramebufferSize(handle, &actualWidth, &actualHeight);
    if (actualWidth != width || actualHeight != height)
        reportFailure("Size callback payload %ix%i does not match framebuffer %ix%i",
                      width, height, actualWidth, actualHeight);

    if (width < 0 || height < 0)
        reportFailure("Size callback received negative size %ix%i", width, height);

    if (window)
        recordEvent(window, EVENT_SIZE, width, height);
}

static void framebufferSizeCallback(GLFWwindow* handle, int width, int height)
{
    int actualWidth, actualHeight;
    TestWindow* window = getTestWindow(handle);

    glfwGetWindowSize(handle, &actualWidth, &actualHeight);
    if (actualWidth != width || actualHeight != height)
        reportFailure("Framebuffer callback payload %ix%i does not match window %ix%i",
                      width, height, actualWidth, actualHeight);

    if (width < 0 || height < 0)
        reportFailure("Framebuffer callback received negative size %ix%i", width, height);

    if (window)
        recordEvent(window, EVENT_FRAMEBUFFER_SIZE, width, height);
}

static void positionCallback(GLFWwindow* handle, int xpos, int ypos)
{
    int actualX, actualY;
    TestWindow* window = getTestWindow(handle);

    glfwGetWindowPos(handle, &actualX, &actualY);
    if (actualX != xpos || actualY != ypos)
        reportFailure("Position callback payload %i,%i does not match window %i,%i",
                      xpos, ypos, actualX, actualY);

    if (window)
        recordEvent(window, EVENT_POS, xpos, ypos);
}

static void iconifyCallback(GLFWwindow* handle, int iconified)
{
    int actual;
    TestWindow* window = getTestWindow(handle);

    actual = glfwGetWindowAttrib(handle, GLFW_ICONIFIED);
    if (actual != iconified)
        reportFailure("Iconify callback payload %i does not match attribute %i",
                      iconified, actual);

    if (iconified != GLFW_TRUE && iconified != GLFW_FALSE)
        reportFailure("Iconify callback received invalid value %i", iconified);

    if (window)
        recordEvent(window, EVENT_ICONIFY, iconified, 0);
}

static void maximizeCallback(GLFWwindow* handle, int maximized)
{
    int actual;
    TestWindow* window = getTestWindow(handle);

    actual = glfwGetWindowAttrib(handle, GLFW_MAXIMIZED);
    if (actual != maximized)
        reportFailure("Maximize callback payload %i does not match attribute %i",
                      maximized, actual);

    if (maximized != GLFW_TRUE && maximized != GLFW_FALSE)
        reportFailure("Maximize callback received invalid value %i", maximized);

    if (window)
        recordEvent(window, EVENT_MAXIMIZE, maximized, 0);
}

static void refreshCallback(GLFWwindow* handle)
{
    TestWindow* window = getTestWindow(handle);

    if (window)
        recordEvent(window, EVENT_REFRESH, 0, 0);
}

static void focusCallback(GLFWwindow* handle, int focused)
{
    int actual;
    TestWindow* window = getTestWindow(handle);

    actual = glfwGetWindowAttrib(handle, GLFW_FOCUSED);
    if (actual != focused)
        reportFailure("Focus callback payload %i does not match attribute %i",
                      focused, actual);

    if (focused != GLFW_TRUE && focused != GLFW_FALSE)
        reportFailure("Focus callback received invalid value %i", focused);

    if (window)
        recordEvent(window, EVENT_FOCUS, focused, 0);
}

static void captureSnapshot(GLFWwindow* handle, Snapshot* snapshot)
{
    glfwGetWindowPos(handle, &snapshot->xpos, &snapshot->ypos);
    glfwGetWindowSize(handle, &snapshot->width, &snapshot->height);
    glfwGetFramebufferSize(handle,
                           &snapshot->framebufferWidth,
                           &snapshot->framebufferHeight);

    snapshot->iconified = glfwGetWindowAttrib(handle, GLFW_ICONIFIED);
    snapshot->maximized = glfwGetWindowAttrib(handle, GLFW_MAXIMIZED);
    snapshot->visible = glfwGetWindowAttrib(handle, GLFW_VISIBLE);
    snapshot->focused = glfwGetWindowAttrib(handle, GLFW_FOCUSED);
}

static void checkSnapshot(const WindowModel* model, const Snapshot* snapshot,
                          int sequence, int group, const char* operation)
{
    context.stateSnapshots++;

    if (snapshot->xpos != model->xpos || snapshot->ypos != model->ypos)
    {
        reportFailure("seq %i group %i %s: position %i,%i, expected %i,%i",
                      sequence, group, operation,
                      snapshot->xpos, snapshot->ypos, model->xpos, model->ypos);
    }

    if (snapshot->width != model->width || snapshot->height != model->height)
    {
        reportFailure("seq %i group %i %s: size %ix%i, expected %ix%i",
                      sequence, group, operation,
                      snapshot->width, snapshot->height,
                      model->width, model->height);
    }

    if (snapshot->iconified != model->iconified)
    {
        reportFailure("seq %i group %i %s: iconified %i, expected %i",
                      sequence, group, operation,
                      snapshot->iconified, model->iconified);
    }

    if (snapshot->maximized != model->maximized)
    {
        reportFailure("seq %i group %i %s: maximized %i, expected %i",
                      sequence, group, operation,
                      snapshot->maximized, model->maximized);
    }

    if (snapshot->visible != model->visible)
    {
        reportFailure("seq %i group %i %s: visible %i, expected %i",
                      sequence, group, operation,
                      snapshot->visible, model->visible);
    }

    if (snapshot->focused != model->focused)
    {
        reportFailure("seq %i group %i %s: focused %i, expected %i",
                      sequence, group, operation,
                      snapshot->focused, model->focused);
    }

    if (snapshot->width < 0 || snapshot->height < 0)
    {
        reportFailure("seq %i group %i %s: negative window size %ix%i",
                      sequence, group, operation,
                      snapshot->width, snapshot->height);
    }

    if (snapshot->framebufferWidth != snapshot->width ||
        snapshot->framebufferHeight != snapshot->height)
    {
        reportFailure("seq %i group %i %s: framebuffer %ix%i differs from window %ix%i",
                      sequence, group, operation,
                      snapshot->framebufferWidth, snapshot->framebufferHeight,
                      snapshot->width, snapshot->height);
    }

    if (snapshot->iconified != GLFW_FALSE && snapshot->iconified != GLFW_TRUE)
    {
        reportFailure("seq %i group %i %s: invalid ICONIFIED attribute %i",
                      sequence, group, operation, snapshot->iconified);
    }

    if (snapshot->maximized != GLFW_FALSE && snapshot->maximized != GLFW_TRUE)
    {
        reportFailure("seq %i group %i %s: invalid MAXIMIZED attribute %i",
                      sequence, group, operation, snapshot->maximized);
    }

    if (snapshot->visible != GLFW_FALSE && snapshot->visible != GLFW_TRUE)
    {
        reportFailure("seq %i group %i %s: invalid VISIBLE attribute %i",
                      sequence, group, operation, snapshot->visible);
    }

    if (snapshot->focused != GLFW_FALSE && snapshot->focused != GLFW_TRUE)
    {
        reportFailure("seq %i group %i %s: invalid FOCUSED attribute %i",
                      sequence, group, operation, snapshot->focused);
    }

    if (glfwGetWindowMonitor(model->test.handle) != NULL)
        reportFailure("seq %i group %i %s: unexpected full-screen monitor",
                      sequence, group, operation);
}

static void addExpectedEvent(EventRecord* expected, int* count,
                             int type, int first, int second)
{
    EventRecord* event = &expected[(*count)++];
    event->type = type;
    event->first = first;
    event->second = second;
    context.expectedEvents[type]++;
}

static void checkEvents(const WindowModel* model, int sequence, int group,
                        const char* operation, int start, int expectedCount,
                        const EventRecord* expected)
{
    int observedCount = model->test.eventCount - start;
    int index;

    context.eventGroups++;

    if (observedCount != expectedCount)
    {
        reportFailure("seq %i group %i %s: observed %i events, expected %i",
                      sequence, group, operation, observedCount, expectedCount);
    }

    for (index = 0;  index < expectedCount && index < observedCount;  index++)
    {
        const EventRecord* actual = &model->test.events[start + index];
        const EventRecord* wanted = &expected[index];

        if (actual->type != wanted->type ||
            actual->first != wanted->first ||
            actual->second != wanted->second)
        {
            reportFailure("seq %i group %i %s: event %i is %s(%i,%i), expected %s(%i,%i)",
                          sequence, group, operation, index,
                          eventNames[actual->type], actual->first, actual->second,
                          eventNames[wanted->type], wanted->first, wanted->second);
        }
    }
}

static void verifyOperation(WindowModel* model, int sequence, int group,
                            const char* operation, int eventStart,
                            int expectedCount, const EventRecord* expected)
{
    Snapshot snapshot;

    glfwPollEvents();
    captureSnapshot(model->test.handle, &snapshot);
    checkEvents(model, sequence, group, operation, eventStart, expectedCount, expected);
    checkSnapshot(model, &snapshot, sequence, group, operation);
}

static int positiveSize(Random* random, int monitorSize)
{
    switch (randomBelow(random, 8))
    {
        case 0:
            return 1;
        case 1:
            return monitorSize;
        case 2:
            return monitorSize * 2;
        case 3:
            return 640;
        case 4:
            return 1920;
        case 5:
            return 4096;
        default:
            return 1 + randomBelow(random, 8192);
    }
}

static void resizeOperation(WindowModel* model, Random* random,
                            int sequence, int group)
{
    EventRecord expected[3];
    int expectedCount = 0;
    int eventStart = model->test.eventCount;
    int width;
    int height;
    int zeroClass = -1;

    if (group == 0)
    {
        width = 0;
        height = 0;
        zeroClass = 0;
    }
    else if (group == 1)
    {
        width = 0;
        height = positiveSize(random, 1080);
        zeroClass = 1;
    }
    else if (group == 2)
    {
        width = positiveSize(random, 1920);
        height = 0;
        zeroClass = 2;
    }
    else
    {
        switch (randomBelow(random, 12))
        {
            case 0:
                width = 0;
                height = 0;
                zeroClass = 0;
                break;
            case 1:
                width = 0;
                height = positiveSize(random, 1080);
                zeroClass = 1;
                break;
            case 2:
                width = positiveSize(random, 1920);
                height = 0;
                zeroClass = 2;
                break;
            default:
                width = positiveSize(random, 1920);
                height = positiveSize(random, 1080);
                break;
        }
    }

    if (zeroClass >= 0)
        context.zeroSize[zeroClass]++;
    if (zeroClass >= 0)
        context.zeroSizeTotal++;

    context.operations[0]++;
    context.layoutOperations[sequence % MAX_LAYOUTS]++;

    if (model->width != width || model->height != height)
    {
        addExpectedEvent(expected, &expectedCount,
                         EVENT_FRAMEBUFFER_SIZE, width, height);
        addExpectedEvent(expected, &expectedCount, EVENT_REFRESH, 0, 0);
        addExpectedEvent(expected, &expectedCount, EVENT_SIZE, width, height);
    }

    glfwSetWindowSize(model->test.handle, width, height);
    model->width = width;
    model->height = height;
    verifyOperation(model, sequence, group, "resize",
                    eventStart, expectedCount, expected);
}

static void iconifyOperation(WindowModel* model, Random* random,
                             int sequence, int group)
{
    EventRecord expected[2];
    int expectedCount = 0;
    int eventStart = model->test.eventCount;

    (void) random;
    context.operations[1]++;
    context.layoutOperations[sequence % MAX_LAYOUTS]++;

    if (!model->iconified)
    {
        if (model->focused)
            addExpectedEvent(expected, &expectedCount, EVENT_FOCUS, GLFW_FALSE, 0);
        addExpectedEvent(expected, &expectedCount, EVENT_ICONIFY, GLFW_TRUE, 0);
    }

    glfwIconifyWindow(model->test.handle);
    model->iconified = GLFW_TRUE;
    model->focused = GLFW_FALSE;
    verifyOperation(model, sequence, group, "iconify",
                    eventStart, expectedCount, expected);
}

static void restoreOperation(WindowModel* model, Random* random,
                             int sequence, int group)
{
    EventRecord expected[2];
    int expectedCount = 0;
    int eventStart = model->test.eventCount;

    (void) random;
    context.operations[2]++;
    context.layoutOperations[sequence % MAX_LAYOUTS]++;

    if (model->iconified)
        addExpectedEvent(expected, &expectedCount, EVENT_ICONIFY, GLFW_FALSE, 0);
    else if (model->maximized)
    {
        addExpectedEvent(expected, &expectedCount, EVENT_MAXIMIZE, GLFW_FALSE, 0);
        model->maximized = GLFW_FALSE;
    }

    glfwRestoreWindow(model->test.handle);
    model->iconified = GLFW_FALSE;
    verifyOperation(model, sequence, group, "restore",
                    eventStart, expectedCount, expected);
}

static void moveOperation(WindowModel* model, Random* random,
                          int sequence, int group)
{
    const MonitorLayout* layout = &layouts[sequence % MAX_LAYOUTS];
    const int monitorIndex = (sequence + group) % layout->monitorCount;
    const Rect* monitor = &layout->monitors[monitorIndex];
    EventRecord expected[1];
    int expectedCount = 0;
    int eventStart = model->test.eventCount;
    int xpos;
    int ypos;

    context.operations[3]++;
    context.layoutOperations[sequence % MAX_LAYOUTS]++;
    context.layoutMoves[sequence % MAX_LAYOUTS]++;
    context.monitorTouches[sequence % MAX_LAYOUTS][monitorIndex]++;

    switch (randomBelow(random, 6))
    {
        case 0:
            xpos = monitor->x;
            ypos = monitor->y;
            break;
        case 1:
            xpos = monitor->x + monitor->width - 1;
            ypos = monitor->y + monitor->height - 1;
            break;
        case 2:
            xpos = monitor->x - 1;
            ypos = monitor->y - 1;
            break;
        case 3:
            xpos = monitor->x + monitor->width;
            ypos = monitor->y + monitor->height;
            break;
        default:
            xpos = monitor->x + randomBelow(random, monitor->width);
            ypos = monitor->y + randomBelow(random, monitor->height);
            break;
    }

    if (model->xpos != xpos || model->ypos != ypos)
        addExpectedEvent(expected, &expectedCount, EVENT_POS, xpos, ypos);

    glfwSetWindowPos(model->test.handle, xpos, ypos);
    model->xpos = xpos;
    model->ypos = ypos;
    verifyOperation(model, sequence, group, "move",
                    eventStart, expectedCount, expected);
}

static int createWindow(WindowModel* model, Random* random, int sequence)
{
    const MonitorLayout* layout = &layouts[sequence % MAX_LAYOUTS];
    const Rect* monitor = &layout->monitors[0];
    int index;
    int width = 320 + randomBelow(random, 961);
    int height = 240 + randomBelow(random, 721);
    int xpos = monitor->x + randomBelow(random, monitor->width - width);
    int ypos = monitor->y + randomBelow(random, monitor->height - height);

    memset(model, 0, sizeof(*model));

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_POSITION_X, xpos);
    glfwWindowHint(GLFW_POSITION_Y, ypos);

    model->test.handle = glfwCreateWindow(width, height, "Window ops property test",
                                          NULL, NULL);
    if (!model->test.handle)
    {
        reportFailure("seq %i: failed to create window", sequence);
        return 0;
    }

    context.windowsCreated++;
    glfwSetWindowUserPointer(model->test.handle, model);
    glfwSetWindowPosCallback(model->test.handle, positionCallback);
    glfwSetWindowSizeCallback(model->test.handle, sizeCallback);
    glfwSetFramebufferSizeCallback(model->test.handle, framebufferSizeCallback);
    glfwSetWindowIconifyCallback(model->test.handle, iconifyCallback);
    glfwSetWindowMaximizeCallback(model->test.handle, maximizeCallback);
    glfwSetWindowRefreshCallback(model->test.handle, refreshCallback);
    glfwSetWindowFocusCallback(model->test.handle, focusCallback);

    model->xpos = xpos;
    model->ypos = ypos;
    model->width = width;
    model->height = height;
    model->visible = GLFW_TRUE;
    model->focused = GLFW_FALSE;

    for (index = 0;  index < 2;  index++)
    {
        Snapshot snapshot;
        captureSnapshot(model->test.handle, &snapshot);
        checkSnapshot(model, &snapshot, sequence, -1, "create");
    }

    return 1;
}

static void runSequence(Random* random, int sequence)
{
    WindowModel model;
    int group;

    if (!createWindow(&model, random, sequence))
        return;

    for (group = 0;  group < GROUPS_PER_SEQUENCE;  group++)
    {
        resizeOperation(&model, random, sequence, group);
        iconifyOperation(&model, random, sequence, group);
        restoreOperation(&model, random, sequence, group);
        moveOperation(&model, random, sequence, group);
    }

    {
        Snapshot snapshot;
        captureSnapshot(model.test.handle, &snapshot);
        checkSnapshot(&model, &snapshot, sequence, GROUPS_PER_SEQUENCE, "final");
    }

    glfwDestroyWindow(model.test.handle);
    context.windowsDestroyed++;
    glfwPollEvents();

    context.sequencesCompleted++;

    if (context.verbose && (sequence + 1) % 100 == 0)
    {
        printf("completed %i/%i sequences\n",
               sequence + 1, context.sequencesRequested);
        fflush(stdout);
    }
}

static int eventStateTotal(const int counts[EVENT_TYPE_COUNT])
{
    return counts[EVENT_POS] +
           counts[EVENT_SIZE] +
           counts[EVENT_ICONIFY] +
           counts[EVENT_MAXIMIZE] +
           counts[EVENT_FOCUS];
}

static int eventDifference(int expected, int observed)
{
    return expected > observed ? expected - observed : 0;
}

static void verifyCoverage(void)
{
    int layoutIndex;
    int zeroClass;
    int required = context.sequencesRequested >= DEFAULT_SEQUENCES;

    if (!required)
        return;

    for (zeroClass = 0;  zeroClass < 3;  zeroClass++)
    {
        if (context.zeroSize[zeroClass] < context.sequencesRequested)
        {
            reportFailure("zero-size class %i observed %i times, expected at least %i",
                          zeroClass, context.zeroSize[zeroClass],
                          context.sequencesRequested);
        }
    }

    for (layoutIndex = 0;  layoutIndex < MAX_LAYOUTS;  layoutIndex++)
    {
        int monitorIndex;

        for (monitorIndex = 0;
             monitorIndex < layouts[layoutIndex].monitorCount;
             monitorIndex++)
        {
            if (context.monitorTouches[layoutIndex][monitorIndex] == 0)
            {
                reportFailure("layout %s did not touch monitor %i",
                              layouts[layoutIndex].name, monitorIndex);
            }
        }
    }
}

static int writeJson(void)
{
    FILE* file;
    int layoutIndex;
    int type;
    int missingEvents = 0;
    int unexpectedEvents = 0;
    int stateExpected;
    int stateObserved;

    if (!context.jsonPath)
        return 1;

    file = fopen(context.jsonPath, "w");
    if (!file)
    {
        reportFailure("could not open JSON output '%s': %s",
                      context.jsonPath, strerror(errno));
        return 0;
    }

    for (type = 0;  type < EVENT_TYPE_COUNT;  type++)
    {
        missingEvents += eventDifference(context.expectedEvents[type],
                                         context.observedEvents[type]);
        unexpectedEvents += eventDifference(context.observedEvents[type],
                                           context.expectedEvents[type]);
    }

    stateExpected = eventStateTotal(context.expectedEvents);
    stateObserved = eventStateTotal(context.observedEvents);

    fprintf(file, "{\n");
    fprintf(file, "  \"result\": \"%s\",\n",
            context.failures == 0 && context.glfwErrors == 0 ? "PASS" : "FAIL");
    fprintf(file, "  \"platform\": \"null\",\n");
    fprintf(file, "  \"seed\": \"%" PRIu64 "\",\n", context.seed);
    fprintf(file, "  \"strict_checks\": true,\n");
    fprintf(file, "  \"sequences_requested\": %i,\n", context.sequencesRequested);
    fprintf(file, "  \"sequences_completed\": %i,\n", context.sequencesCompleted);
    fprintf(file, "  \"operation_groups\": %i,\n", context.operationGroups);
    fprintf(file, "  \"operations_attempted\": %i,\n", context.operationsAttempted);
    fprintf(file, "  \"operations\": {\n");
    fprintf(file, "    \"resize\": %i,\n", context.operations[0]);
    fprintf(file, "    \"iconify\": %i,\n", context.operations[1]);
    fprintf(file, "    \"restore\": %i,\n", context.operations[2]);
    fprintf(file, "    \"move\": %i\n", context.operations[3]);
    fprintf(file, "  },\n");
    fprintf(file, "  \"zero_size\": {\n");
    fprintf(file, "    \"0x0\": %i,\n", context.zeroSize[0]);
    fprintf(file, "    \"0xH\": %i,\n", context.zeroSize[1]);
    fprintf(file, "    \"Wx0\": %i,\n", context.zeroSize[2]);
    fprintf(file, "    \"total\": %i\n", context.zeroSizeTotal);
    fprintf(file, "  },\n");
    fprintf(file, "  \"monitors\": {\n");
    fprintf(file, "    \"actual_glfw_count\": %i,\n", context.actualMonitorCount);
    fprintf(file, "    \"simulated_max\": %i,\n", MAX_MONITORS);
    fprintf(file, "    \"layouts\": [\n");

    for (layoutIndex = 0;  layoutIndex < MAX_LAYOUTS;  layoutIndex++)
    {
        int monitorIndex;
        int touched = 0;

        for (monitorIndex = 0;
             monitorIndex < layouts[layoutIndex].monitorCount;
             monitorIndex++)
        {
            if (context.monitorTouches[layoutIndex][monitorIndex] > 0)
                touched++;
        }

        fprintf(file,
                "      {\"name\": \"%s\", \"monitors\": %i, "
                "\"operations\": %i, \"moves\": %i, "
                "\"monitors_touched\": %i}%s\n",
                layouts[layoutIndex].name,
                layouts[layoutIndex].monitorCount,
                context.layoutOperations[layoutIndex],
                context.layoutMoves[layoutIndex],
                touched,
                layoutIndex + 1 < MAX_LAYOUTS ? "," : "");
    }

    fprintf(file, "    ]\n");
    fprintf(file, "  },\n");
    fprintf(file, "  \"windows\": {\n");
    fprintf(file, "    \"created\": %i,\n", context.windowsCreated);
    fprintf(file, "    \"destroyed\": %i\n", context.windowsDestroyed);
    fprintf(file, "  },\n");
    fprintf(file, "  \"events\": {\n");
    fprintf(file, "    \"expected_state_events\": %i,\n", stateExpected);
    fprintf(file, "    \"observed_state_events\": %i,\n", stateObserved);
    fprintf(file, "    \"missing_state_events\": %i,\n",
            eventDifference(stateExpected, stateObserved));
    fprintf(file, "    \"unexpected_state_events\": %i,\n",
            eventDifference(stateObserved, stateExpected));
    fprintf(file, "    \"expected_total\": %i,\n",
            context.expectedEvents[EVENT_SIZE] +
            context.expectedEvents[EVENT_FRAMEBUFFER_SIZE] +
            stateExpected +
            context.expectedEvents[EVENT_REFRESH]);
    fprintf(file, "    \"observed_total\": %i,\n",
            context.observedEvents[EVENT_SIZE] +
            context.observedEvents[EVENT_FRAMEBUFFER_SIZE] +
            stateObserved +
            context.observedEvents[EVENT_REFRESH]);
    fprintf(file, "    \"missing\": %i,\n", missingEvents);
    fprintf(file, "    \"unexpected\": %i,\n", unexpectedEvents);
    fprintf(file, "    \"refresh_events\": %i,\n",
            context.observedEvents[EVENT_REFRESH]);
    fprintf(file, "    \"by_type\": {\n");

    for (type = 0;  type < EVENT_TYPE_COUNT;  type++)
    {
        fprintf(file,
                "      \"%s\": {\"expected\": %i, \"observed\": %i}%s\n",
                eventNames[type],
                context.expectedEvents[type],
                context.observedEvents[type],
                type + 1 < EVENT_TYPE_COUNT ? "," : "");
    }

    fprintf(file, "    }\n");
    fprintf(file, "  },\n");
    fprintf(file, "  \"checks\": {\n");
    fprintf(file, "    \"state_snapshots\": %i,\n", context.stateSnapshots);
    fprintf(file, "    \"event_groups\": %i,\n", context.eventGroups);
    fprintf(file, "    \"failures\": %i,\n", context.failures);
    fprintf(file, "    \"glfw_errors\": %i,\n", context.glfwErrors);
    fprintf(file, "    \"last_glfw_error\": %i\n", context.lastGlfwError);
    fprintf(file, "  },\n");
    fprintf(file, "  \"crashes\": 0,\n");
    fprintf(file, "  \"elapsed_seconds\": %.6f\n", context.elapsedSeconds);
    fprintf(file, "}\n");

    if (fclose(file) != 0)
    {
        reportFailure("could not close JSON output '%s': %s",
                      context.jsonPath, strerror(errno));
        return 0;
    }

    return 1;
}

static void printSummary(void)
{
    int layoutIndex;

    printf("\nWindow operation property summary\n");
    printf("  result:             %s\n",
           context.failures == 0 && context.glfwErrors == 0 ? "PASS" : "FAIL");
    printf("  platform:           null\n");
    printf("  seed:               %" PRIu64 "\n", context.seed);
    printf("  sequences:          %i/%i\n",
           context.sequencesCompleted, context.sequencesRequested);
    printf("  operation groups:   %i\n", context.operationGroups);
    printf("  operations:         %i (", context.operationsAttempted);
    printf("resize %i, iconify %i, restore %i, move %i)\n",
           context.operations[0], context.operations[1],
           context.operations[2], context.operations[3]);
    printf("  zero sizes:         %i total (0x0 %i, 0xH %i, Wx0 %i)\n",
           context.zeroSizeTotal, context.zeroSize[0],
           context.zeroSize[1], context.zeroSize[2]);
    printf("  monitors:           %i actual, %i simulated maximum\n",
           context.actualMonitorCount, MAX_MONITORS);

    for (layoutIndex = 0;  layoutIndex < MAX_LAYOUTS;  layoutIndex++)
    {
        int monitorIndex;
        int touched = 0;

        for (monitorIndex = 0;
             monitorIndex < layouts[layoutIndex].monitorCount;
             monitorIndex++)
        {
            if (context.monitorTouches[layoutIndex][monitorIndex] > 0)
                touched++;
        }

        printf("    %-24s %2i monitors, %5i ops, %2i/%2i touched\n",
               layouts[layoutIndex].name,
               layouts[layoutIndex].monitorCount,
               context.layoutOperations[layoutIndex],
               touched, layouts[layoutIndex].monitorCount);
    }

    printf("  windows:            %i created, %i destroyed\n",
           context.windowsCreated, context.windowsDestroyed);
    printf("  events:             %i expected, %i observed, %i missing, %i unexpected\n",
           context.expectedEvents[EVENT_SIZE] +
           context.expectedEvents[EVENT_FRAMEBUFFER_SIZE] +
           eventStateTotal(context.expectedEvents) +
           context.expectedEvents[EVENT_REFRESH],
           context.observedEvents[EVENT_SIZE] +
           context.observedEvents[EVENT_FRAMEBUFFER_SIZE] +
           eventStateTotal(context.observedEvents) +
           context.observedEvents[EVENT_REFRESH],
           eventDifference(eventStateTotal(context.expectedEvents),
                           eventStateTotal(context.observedEvents)),
           eventDifference(eventStateTotal(context.observedEvents),
                           eventStateTotal(context.expectedEvents)));
    printf("  checks:             %i snapshots, %i event groups\n",
           context.stateSnapshots, context.eventGroups);
    printf("  failures:           %i\n", context.failures);
    printf("  GLFW errors:        %i (last 0x%08X)\n",
           context.glfwErrors, context.lastGlfwError);
    printf("  crashes:            0\n");
    printf("  elapsed:            %.6f s\n", context.elapsedSeconds);
}

static void usage(const char* executable)
{
    printf("Usage: %s [options]\n", executable);
    printf("Options:\n");
    printf("  -n, --sequences N  number of sequences (default %i)\n", DEFAULT_SEQUENCES);
    printf("  -s, --seed N       unsigned decimal or 0x-prefixed seed\n");
    printf("  -j, --json FILE    write machine-readable summary to FILE\n");
    printf("  -v, --verbose      print progress every 100 sequences\n");
    printf("  -h, --help         show this help\n");
}

static int parseUnsigned(const char* text, uint64_t* value)
{
    char* end = NULL;
    unsigned long long parsed;

    if (!text || !*text || *text == '-')
        return 0;

    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || !end || *end != '\0')
        return 0;

    *value = (uint64_t) parsed;
    return 1;
}

static int parseArguments(int argc, char** argv)
{
    int index = 1;

    while (index < argc)
    {
        const char* argument = argv[index++];

        if (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0)
        {
            usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(argument, "-v") == 0 || strcmp(argument, "--verbose") == 0)
        {
            context.verbose = 1;
        }
        else if (strcmp(argument, "-n") == 0 || strcmp(argument, "--sequences") == 0)
        {
            uint64_t value;

            if (index >= argc || !parseUnsigned(argv[index++], &value))
            {
                fprintf(stderr, "Invalid sequence count\n");
                return 0;
            }

            context.sequencesRequested = (int) value;
            if (context.sequencesRequested <= 0 ||
                (uint64_t) context.sequencesRequested != value)
            {
                fprintf(stderr, "Invalid sequence count\n");
                return 0;
            }
        }
        else if (strcmp(argument, "-s") == 0 || strcmp(argument, "--seed") == 0)
        {
            if (index >= argc || !parseUnsigned(argv[index++], &context.seed))
            {
                fprintf(stderr, "Invalid seed\n");
                return 0;
            }
        }
        else if (strcmp(argument, "-j") == 0 || strcmp(argument, "--json") == 0)
        {
            if (index >= argc)
            {
                fprintf(stderr, "Missing JSON output path\n");
                return 0;
            }

            context.jsonPath = argv[index++];
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argument);
            return 0;
        }
    }

    return 1;
}

int main(int argc, char** argv)
{
    Random random;
    int sequence;
    int actualMonitorCount = 0;

    memset(&context, 0, sizeof(context));
    context.seed = DEFAULT_SEED;
    context.sequencesRequested = DEFAULT_SEQUENCES;

    if (!parseArguments(argc, argv))
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    initializeLayouts();
    glfwSetErrorCallback(reportError);
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);

    if (!glfwInit())
    {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return EXIT_FAILURE;
    }

    context.actualMonitorCount = actualMonitorCount;
    {
        GLFWmonitor** monitors = glfwGetMonitors(&actualMonitorCount);
        context.actualMonitorCount = actualMonitorCount;

        if (actualMonitorCount <= 0 || monitors == NULL)
            reportFailure("Null platform reported no monitors");
    }

    if (glfwGetPlatform() != GLFW_PLATFORM_NULL)
    {
        reportFailure("Expected GLFW_PLATFORM_NULL, got 0x%08X", glfwGetPlatform());
        glfwTerminate();
        return EXIT_FAILURE;
    }

    random.state = context.seed ? context.seed : UINT64_C(1);

    printf("Running %i random window operation sequences with seed %" PRIu64 "\n",
           context.sequencesRequested, context.seed);
    fflush(stdout);

    {
        const uint64_t frequency = glfwGetTimerFrequency();
        const uint64_t start = glfwGetTimerValue();

        for (sequence = 0;  sequence < context.sequencesRequested;  sequence++)
        {
            context.operationGroups += GROUPS_PER_SEQUENCE;
            context.operationsAttempted += GROUPS_PER_SEQUENCE * 4;
            runSequence(&random, sequence);
        }

        context.elapsedSeconds = frequency ?
            (double) (glfwGetTimerValue() - start) / (double) frequency : 0.0;
    }

    verifyCoverage();
    glfwTerminate();
    writeJson();
    printSummary();

    if (context.jsonPath)
        printf("  JSON:               %s\n", context.jsonPath);

    if (context.failures != 0 || context.glfwErrors != 0)
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
