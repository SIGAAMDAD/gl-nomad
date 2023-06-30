#include "n_shared.h"
#include "g_game.h"
#include "m_renderer.h"
#include "n_scf.h"

static constexpr const char *TEXMAP_PATH = "Files/gamedata/RES/texmap.bmp";

Renderer* renderer;

cvar_t r_ticrate           = {"r_ticrate", "", 0.0f, 35, qfalse, TYPE_INT, CVAR_SAVE};
cvar_t r_texture_magfilter = {"r_texture_magfilter", "GL_NEAREST", 0.0f, 0, qfalse, TYPE_STRING, CVAR_SAVE};
cvar_t r_texture_minfilter = {"r_texture_minfilter", "GL_LINEAR_MIPMAP_LINEAR", 0.0f, 0, qfalse, TYPE_STRING, CVAR_SAVE};
cvar_t r_screenheight      = {"r_screenheight", "", 0.0f, 720, qfalse, TYPE_INT, CVAR_SAVE | CVAR_ROM};
cvar_t r_screenwidth       = {"r_screenwidth", "", 0.0f, 1024, qfalse, TYPE_INT, CVAR_SAVE | CVAR_ROM};
cvar_t r_vsync             = {"r_vsync", "", 0.0f, 0, qtrue, TYPE_BOOL, CVAR_SAVE};
cvar_t r_fullscreen        = {"r_fullscreen", "", 0.0f, 0, qtrue, TYPE_BOOL, CVAR_SAVE};
cvar_t r_native_fullscreen = {"r_native_fullscreen", "", 0.0f, 0, qfalse, TYPE_BOOL, CVAR_SAVE};
cvar_t r_hidden            = {"r_hidden", "", 0.0f, 0, qfalse, TYPE_BOOL, CVAR_SAVE};
cvar_t r_drawFPS           = {"r_drawFPS", "", 0.0f, 0, qfalse, TYPE_BOOL, CVAR_SAVE};
cvar_t r_renderapi         = {"r_renderapi", "", 0.0f, (int32_t)R_OPENGL, qfalse, TYPE_INT, CVAR_SAVE};
cvar_t r_msaa_amount       = {"r_msaa_amount", "", 0.0f, 2, qfalse, TYPE_INT, CVAR_SAVE};
cvar_t r_dither            = {"r_dither", "", 0.0f, 0, qfalse, TYPE_BOOL, CVAR_SAVE};

const uint32_t width = 56;
const uint32_t height = 48;

typedef struct
{
    const uint32_t maxQuads = RENDER_MAX_QUADS;
    const uint32_t maxVertices = RENDER_MAX_VERTICES;
    const uint32_t maxIndices = RENDER_MAX_INDICES;

    uint32_t *indices;
    uint32_t numSprites;
    uint32_t numPints;

    char vmatrix[height][width];

    vertex_t *pintVerts;
    shader_t *pintShader;
    shader_t *spriteShader;
    vertexCache_t *pintCache;
    vertexCache_t *spriteCache;
    SpriteSheet *sprSheet;
} frameData_t;

static frameData_t frame;

bool imgui_on = false;

static void R_InitVK(void)
{
    renderer->gpuContext.instance = (VKContext *)Hunk_Alloc(sizeof(VKContext), "VKContext", h_high);

    VkApplicationInfo *info = &renderer->gpuContext.instance->appInfo;
    memset(info, 0, sizeof(VkApplicationInfo));
    info->sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    info->pApplicationName = "The Nomad";
    info->applicationVersion = VK_MAKE_VERSION(1, 1, 0);
    info->pEngineName = "GDRLib";
    info->engineVersion = VK_MAKE_VERSION(0, 0, 1);
    info->apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo *createInfo = &renderer->gpuContext.instance->createInfo;
    memset(createInfo, 0, sizeof(VkInstanceCreateInfo));
    createInfo->sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo->pApplicationInfo = info;

    VkResult result = vkCreateInstance(createInfo, NULL, &renderer->gpuContext.instance->instance);
    if (result != VK_SUCCESS)
        N_Error("R_Init: failed to initialize a vulkan instance");

    VkPhysicalDevice device = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(renderer->gpuContext.instance->instance, &deviceCount, NULL);
    if (!deviceCount)
        N_Error("R_Init: failed to get physical device count for vulkan instance");
    
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(renderer->gpuContext.instance->instance, &deviceCount, devices.data());

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices.front(), &properties);

    SDL_Vulkan_CreateSurface(renderer->window, renderer->gpuContext.instance->instance, &renderer->gpuContext.instance->surface);

    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(renderer->gpuContext.instance->device, 1, renderer->gpuContext.instance->surface, &presentSupport);
}

static void R_InitImGui(void)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(renderer->window, renderer->gpuContext.context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImGui_ImplOpenGL3_CreateDeviceObjects();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    
    imgui_on = true;
}

static void R_GfxInfo_f(void)
{
    Con_Printf("\n\n<---------- Gfx Info ---------->");
    Con_Printf("Rendering API: %s", r_renderapi.s);
    
    if (N_strcmp("R_OPENGL", r_renderapi.s)) {
        Con_Printf("=== OpenGL Driver Info ===");
        Con_Printf("GL_RENDERER: %s", glGetString(GL_RENDERER));
        Con_Printf("GL_VERSION: %s", glGetString(GL_VERSION));
        Con_Printf("GL_VENDOR: %s", glGetString(GL_VENDOR));
    }
    Con_Printf("=== Cvars ===");
    Con_Printf("r_screenwidth: %i", r_screenwidth.i);
    Con_Printf("r_screenheight: %i", r_screenheight.i);
    Con_Printf("r_drawFPS: %s", N_booltostr(r_drawFPS.b));
    Con_Printf("r_ticrate: %i", r_ticrate.i);
    Con_Printf("r_fullscreen: %s", N_booltostr(r_fullscreen.b));
    Con_Printf("r_msaa_amount: x%i", r_msaa_amount.i);
    Con_Printf("r_vsync: %s", N_booltostr(r_vsync.b));
//    Con_Printf("r_gl_extensions: %s", N_booltostr(r_gl_extensions.b));
    Con_Printf("r_texture_minfilter: %s", r_texture_minfilter.s);
    Con_Printf("r_texture_magfilter: %s", r_texture_magfilter.s);
    Con_Printf("r_dither: %s", N_booltostr(r_dither.b));
    Con_Printf(" ");
}

void RE_InitSettings_f(void)
{
    // anti-aliasing/multisampling
    if (r_msaa_amount.i > 0)
        glEnable(GL_MULTISAMPLE_ARB);
    else
        glDisable(GL_MULTISAMPLE_ARB);

    // dither
    if (r_dither.b)
        glEnable(GL_DITHER);
    else
        glDisable(GL_DITHER);
}

void RE_SetDefaultState(void)
{
    glEnable(GL_MULTISAMPLE_ARB);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);

    glDisable(GL_DITHER);
    glDisable(GL_CULL_FACE);
}

static void R_InitGL(void)
{
    renderer->gpuContext.context = SDL_GL_CreateContext(renderer->window);
    SDL_GL_MakeCurrent(renderer->window, renderer->gpuContext.context);
    SDL_GL_SetSwapInterval(1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
//   SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
//    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
//    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
        N_Error("R_Init: failed to initialize OpenGL (glad)");
    
    Con_Printf(
        "OpenGL Info:\n"
        "  Version: %s\n"
        "  Renderer: %s\n"
        "  Vendor: %s\n",
    glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_VENDOR));

    const GLenum params[] = {
        GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
        GL_MAX_CUBE_MAP_TEXTURE_SIZE,
        GL_MAX_DRAW_BUFFERS,
        GL_MAX_FRAGMENT_UNIFORM_COMPONENTS,
        GL_MAX_TEXTURE_IMAGE_UNITS,
        GL_MAX_TEXTURE_SIZE,
        GL_MAX_VARYING_FLOATS,
        GL_MAX_VERTEX_ATTRIBS,
        GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS,
        GL_MAX_VERTEX_UNIFORM_COMPONENTS,
        GL_MAX_VIEWPORT_DIMS,
        GL_STEREO,
    };
    const char *names[] = {
        "GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS",
        "GL_MAX_CUBE_MAP_TEXTURE_SIZE",
        "GL_MAX_DRAW_BUFFERS",
        "GL_MAX_FRAGMENT_UNIFORM_COMPONENTS",
        "GL_MAX_TEXTURE_IMAGE_UNITS",
        "GL_MAX_TEXTURE_SIZE",
        "GL_MAX_VARYING_FLOATS",
        "GL_MAX_VERTEX_ATTRIBS",
        "GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS",
        "GL_MAX_VERTEX_UNIFORM_COMPONENTS",
        "GL_MAX_VIEWPORT_DIMS",
        "GL_STEREO",
    };

    Con_Printf("<-------- OpenGL Context Parameters -------->");
    // integers - only works if the order is 0-10 integer return types
    for (int i = 0; i < 10; i++) {
        int v = 0;
        glGetIntegerv(params[i], &v);
        Con_Printf("%s: %i" , names[i], v);
    }
    // others
    int v[2];
    memset(v, 0, sizeof(v));
    glGetIntegerv(params[10], v);
    Con_Printf("%s: %i %i", names[10], v[0], v[1]);
    unsigned char s = 0;
    glGetBooleanv(params[11], &s);
    Con_Printf("%s: %i", names[11], (unsigned int)s);
    Con_Printf(" ");

    Con_Printf("<-------- OpenGL Extensions Info -------->");
    uint32_t extension_count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, (int *)&extension_count);
    Con_Printf("Extension Count: %i", extension_count);
    Con_Printf("Extension List:");
    for (uint32_t i = 0; i < extension_count; ++i) {
        if (i >= 30) { // dont go crazy
            Con_Printf("... (%i more extensions)", extension_count - i);
            break;
        }
        const GLubyte* name = glGetStringi(GL_EXTENSIONS, i);
        api_extensions.emplace_back(name);
        Con_Printf("%s", (const char *)name);
    }
    Con_Printf(" ");

#ifdef _NOMAD_DEBUG
    Con_Printf("turning on OpenGL debug callbacks");
    if (glDebugMessageControlARB != NULL) {
        glEnable(GL_DEBUG_OUTPUT_KHR);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);
        glDebugMessageCallbackARB((GLDEBUGPROCARB)DBG_GL_ErrorCallback, NULL);
        GLuint unusedIds = 0;
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &unusedIds, GL_TRUE);
    }
#endif

    // always-on features
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
//    glEnable(GL_FRAMEBUFFER_SRGB);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);

    RE_InitSettings_f();
    RE_InitFramebuffers();

    Cmd_AddCommand("gfxinfo", R_GfxInfo_f);
    Cmd_AddCommand("gfxrestart", RE_InitSettings_f);
}


static Uint32 R_GetWindowFlags(void)
{
    Uint32 flags = 0;

    if (r_renderapi.i == R_OPENGL)
        flags |= SDL_WINDOW_OPENGL;
    else if (r_renderapi.i == R_SDL2)
        N_Error("R_Init: SDL2 rendering isn't yet supported");
    else if (r_renderapi.i == R_VULKAN)
        N_Error("R_Init: Vulkan rendering isn't yet supported, will be though very soon");
    else
        N_Error("R_Init: unkown rendering api %i", r_renderapi.i);
    
    if (r_hidden.b)
        flags |= SDL_WINDOW_HIDDEN;
    
    if (r_fullscreen.b && !r_native_fullscreen.b)
        flags |= SDL_WINDOW_FULLSCREEN;
    
    flags |= SDL_WINDOW_RESIZABLE;
    return flags;
}

static bool sdl_on = false;

Renderer::Renderer()
    : camera(-3.0f, 3.0f, -3.0f, 3.0f)
{
}

void R_Init(void)
{
    renderer = (Renderer *)Hunk_Alloc(sizeof(Renderer), "renderer", h_high);
    new (renderer) Renderer();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        N_Error("R_Init: failed to initialize SDL2, error message: %s",
            SDL_GetError());
    }

    Con_Printf("alllocating memory to the SDL_Window context");
    renderer->window = SDL_CreateWindow(
                            "The Nomad",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            r_screenwidth.i, r_screenheight.i,
                            SDL_WINDOW_OPENGL
                        );
    if (!renderer->window) {
        N_Error("R_Init: failed to initialize an SDL2 window, error message: %s",
            SDL_GetError());
    }
    assert(renderer->window);
    R_InitGL();

    renderer->numTextures = 0;
    renderer->numVertexCaches = 0;
    renderer->numFBOs = 0;
    renderer->numShaders = 0;

    R_InitImGui();
    sdl_on = true;

    EASY_PROFILER_ENABLE;
    profiler::startListen();
}

void R_ShutDown()
{
    if (!sdl_on)
        return;

    Con_Printf("R_ShutDown: deallocating SDL2 contexts and window");

    if (imgui_on) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        imgui_on = false;
    }
    profiler::dumpBlocksToFile("gamedata/profiler.prof");
    profiler::stopListen();

    RE_ShutdownFramebuffers();
    for (uint32_t i = 0; i < renderer->numShaders; i++) {
        glDeleteProgram(renderer->shaders[i]->id);
    }
    for (uint32_t i = 0; i < renderer->numVertexCaches; i++) {
        RGL_ShutdownCache(renderer->vertexCaches[i]);
    }
    for (uint32_t i = 0; i < renderer->numTextures; i++) {
        glDeleteTextures(1, &renderer->textures[i]->id);
    }

    if (r_renderapi.i == R_OPENGL && renderer->gpuContext.context) {
        SDL_GL_DeleteContext(renderer->gpuContext.context);
        renderer->gpuContext.context = NULL;
    }
    else if (r_renderapi.i == R_VULKAN && renderer->gpuContext.instance) {
        vkDestroySurfaceKHR(renderer->gpuContext.instance->instance, renderer->gpuContext.instance->surface, NULL);
        vkDestroyInstance(renderer->gpuContext.instance->instance, NULL);
        renderer->gpuContext.instance = NULL;
    }
    if (renderer->window) {
        SDL_DestroyWindow(renderer->window);
        renderer->window = NULL;
    }
    SDL_Quit();
    sdl_on = false;
}

#ifdef _NOMAD_DEBUG

const char *DBG_GL_SourceToStr(GLenum source)
{
    switch (source) {
    case GL_DEBUG_SOURCE_API: return "API";
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "Window System";
    case GL_DEBUG_SOURCE_SHADER_COMPILER: return "Shader Compiler";
    case GL_DEBUG_SOURCE_THIRD_PARTY: return "Third Party";
    case GL_DEBUG_SOURCE_APPLICATION: return "Application User";
    case GL_DEBUG_SOURCE_OTHER: return "Other";
    };
    return "Unknown Source";
}

const char *DBG_GL_TypeToStr(GLenum type)
{
    switch (type) {
    case GL_DEBUG_TYPE_ERROR: return "Error";
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "Deprecated Behaviour";
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "Undefined Behaviour";
    case GL_DEBUG_TYPE_PORTABILITY: return "Portability";
    case GL_DEBUG_TYPE_PERFORMANCE: return "Performance";
    case GL_DEBUG_TYPE_MARKER: return "Marker";
    case GL_DEBUG_TYPE_PUSH_GROUP: return "Debug Push group";
    case GL_DEBUG_TYPE_POP_GROUP: return "Debug Pop Group";
    case GL_DEBUG_TYPE_OTHER: return "Other";
    };
    return "Unknown Type";
}

const char *DBG_GL_SeverityToStr(GLenum severity)
{
    switch (severity) {
    case GL_DEBUG_SEVERITY_HIGH: return "High";
    case GL_DEBUG_SEVERITY_MEDIUM: return "Medium";
    case GL_DEBUG_SEVERITY_LOW: return "Low";
    case GL_DEBUG_SEVERITY_NOTIFICATION: return "Notification";
    };
    return "Unknown Severity";
}

void DBG_GL_ErrorCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message, const GLvoid *userParam)
{
    // nothing massive or useless
    if (length >= 300 || severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return;
    }
    switch (type) {
    case GL_DEBUG_TYPE_ERROR:
        Con_Error("[OpenGL Debug Log] %s", message);
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
    case GL_DEBUG_TYPE_PORTABILITY:
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
    case GL_DEBUG_TYPE_PERFORMANCE:
        Con_Printf("WARNING: [OpenGL Debug Log] %s", message);
        break;
    default:
        Con_Printf("[OpenGL Debug Log] %s", message);
        break;
    };
}

#endif

static void R_CameraInfo_f(void)
{
    Con_Printf("\n<---------- Camera Info ---------->");
    Con_Printf(
        "Rotation (Degrees): %f\n"
        "Rotation (Radians): %f\n"
        "Position: (x) %f, (y) %f, (z) %f\n"
        "Zoom Level: %f\n",
    renderer->camera.GetRotation(), glm::radians(renderer->camera.GetRotation()),
    renderer->camera.GetPos().x, renderer->camera.GetPos().y, renderer->camera.GetPos().z,
    renderer->camera.GetZoom());
}

Camera::Camera(float left, float right, float bottom, float top)
    : m_ProjectionMatrix(glm::ortho(left, right, bottom, top, -1.0f, 1.0f)), m_ViewMatrix(1.0f)
{
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_CameraPos)
                        * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));
    
    m_ViewMatrix = glm::inverse(transform);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
    m_ZoomLevel = left;
    m_CameraPos = glm::vec3(0.0f, 3.5f, 0.0f);
    m_AspectRatio = r_screenwidth.i / r_screenheight.i;

    Cmd_AddCommand("gfx_camerainfo", R_CameraInfo_f);
}

void Camera::SetProjection(float left, float right, float bottom, float top)
{
    m_ProjectionMatrix = glm::ortho(left, right, bottom, top);
    CalculateViewMatrix();
}

void Camera::CalculateViewMatrix()
{
    EASY_FUNCTION();
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_CameraPos)
                        * glm::rotate(glm::mat4(1.0f), glm::radians(m_Rotation), glm::vec3(0, 0, 1));
    
    m_ViewMatrix = glm::inverse(transform);
    m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
}

void Camera::ZoomIn(void)
{
    if (!m_ZoomLevel)
        return;

    m_ZoomLevel -= 0.05f;
    SetProjection(-m_ZoomLevel, m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
}

void Camera::ZoomOut(void)
{
    if (m_ZoomLevel >= 15.0f)
        return;

    m_ZoomLevel += 0.05f;
    SetProjection(-m_ZoomLevel, m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
}

void Camera::RotateRight(void)
{
    m_Rotation += 0.75f;
    if (m_Rotation > 180.0f) {
        m_Rotation -= 360.0f;
    }
    else if (m_Rotation <= -180.0f) {
        m_Rotation += 360.0f;
    }
}

void Camera::RotateLeft(void)
{
    m_Rotation -= 0.75f;
    if (m_Rotation > 180.0f) {
        m_Rotation -= 360.0f;
    }
    else if (m_Rotation <= -180.0f) {
        m_Rotation += 360.0f;
    }
}

void Camera::Resize(void)
{
    m_AspectRatio = r_screenwidth.i / r_screenheight.i;
    SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
}

void Camera::MoveUp(void)
{
    m_CameraPos.x += -sin(glm::radians(m_Rotation)) * 0.25f;
    m_CameraPos.y += cos(glm::radians(m_Rotation)) * 0.25f;

    if (renderer->camera.GetPos().y >= 4.0f)
        renderer->camera.GetPos().y = 4.0f;
}

void Camera::MoveDown(void)
{
    m_CameraPos.x -= -sin(glm::radians(m_Rotation)) * 0.25f;
    m_CameraPos.y -= cos(glm::radians(m_Rotation)) * 0.25f;

    if (renderer->camera.GetPos().y <= 2.7f)
        renderer->camera.GetPos().y = 2.6f;
}

void Camera::MoveLeft(void)
{
    m_CameraPos.x -= cos(glm::radians(m_Rotation)) * 0.25f;
    m_CameraPos.y -= sin(glm::radians(m_Rotation)) * 0.25f;

    if (renderer->camera.GetPos().x <= -2.5f)
        renderer->camera.GetPos().x = -2.4f;
}

void Camera::MoveRight(void)
{
    m_CameraPos.x += cos(glm::radians(m_Rotation)) * 0.25f;
    m_CameraPos.y += sin(glm::radians(m_Rotation)) * 0.25f;

    if (renderer->camera.GetPos().x >= 2.3f)
        renderer->camera.GetPos().x = 2.2f;
}

#if 0

int R_DrawMenu(const char* fontfile, const nomadvector<std::string>& choices,
    const char* title)
{
    assert(fontfile);
    assert(title);
    assert(choices.size() > 0);

    std::string font_name = "Files/gamedata/RES/"+std::string(fontfile);
    int x, y;
    int NUMMENU = choices.size();
    std::vector<SDL_Texture*> menus(choices.size());
    std::vector<bool> selected(choices.size());
    std::vector<SDL_Rect> pos(choices.size());
    
    SDL_Color color[2] = {{255,255,255},{255,0,0}};
    int32_t text_size = DEFAULT_TEXT_SIZE;

    TTF_Font *font = TTF_OpenFont(font_name.c_str(), text_size);
    if (!font) {
        N_Error("R_DrawMenu: TTF_OpenFont returned NULL for font file %s", fontfile);
    }
#if 0
    for (int i = 0; i < NUMMENU; ++i) {
        menus[i] = R_GenTextureFromFont(font, choices[i].c_str(), color[0]);
    }
    // get the title
    SDL_Texture* title_texture = R_GenTextureFromFont(font, title, {0, 255, 255});
#endif

    int w, h;
//    SDL_Call(SDL_QueryTexture(title_texture, NULL, NULL, &w, &h));
    SDL_Rect title_rect;
    title_rect.x = 75;
    title_rect.y = 50;
    title_rect.w = w;
    title_rect.h = h;

    int round = 0;
    for (int i = 0; i < NUMMENU; ++i) {
//        SDL_Call(SDL_QueryTexture(menus[i], NULL, NULL, &w, &h));
        pos[i].x = 100;
        pos[i].y = 150 + round;
        pos[i].w = w;
        pos[i].h = h;
        round += 70;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    SDL_Event event;
    while (1) {
        N_DebugWindowClear();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                for (auto* i : menus) {
                    SDL_DestroyTexture(i);
                }
                Game::Get()->~Game();
                exit(EXIT_SUCCESS);
            }
            switch (event.type) {
            case SDL_MOUSEMOTION: {
                y = event.motion.y;
                x = event.motion.x;
                for (int i = 0; i < NUMMENU; i++) {
                    if (x >= pos[i].x && x <= pos[i].x + pos[i].w
                     && y >= pos[i].y && y <= pos[i].y + pos[i].h) {
                        if (!selected[i]) {
                            selected[i] = true;
                            SDL_DestroyTexture(menus[i]);
//                            menus[i] = R_GenTextureFromFont(font, choices[i].c_str(), color[1]);
                        }
                    }
                    else {
                        if (selected[i]) {
                            selected[i] = false;
                            SDL_DestroyTexture(menus[i]);
//                            menus[i] = R_GenTextureFromFont(font, choices[i].c_str(), color[0]);
                        }
                    }
                }
                break; }
            case SDL_MOUSEBUTTONDOWN: {
                x = event.button.x;
                y = event.button.y;
                for(int i = 0; i < NUMMENU; i++) {
                    if(x >= pos[i].x && x <= pos[i].x + pos[i].w
                    && y >= pos[i].y && y <= pos[i].y + pos[i].h) {
                        for (auto* i : menus) {
                            SDL_DestroyTexture(i);
                        }
                        N_DebugWindowDraw();
                        return i;
                    }
                }
                break; }
            case SDL_KEYDOWN: {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    goto done;
                }
                break; }
            };
        }
//        R_DrawTexture(title_texture, NULL, &title_rect);
        for (int i = 0; i < NUMMENU; ++i)
//            R_DrawTexture(menus[i], NULL, &pos[i]);
        N_DebugWindowDraw();
    }
done:
    N_DebugWindowDraw();
    return -1;
}
#endif

bool R_CullVertex(const glm::vec2& pos)
{
    const glm::ivec2 endpos = glm::ivec2(
        Game::Get()->cameraPos.x + 32,
        Game::Get()->cameraPos.y + 12
    );
    const glm::ivec2 startpos = glm::ivec2(
        Game::Get()->cameraPos.x - 32,
        Game::Get()->cameraPos.y - 12
    );

    if ((pos.x <= startpos.x || pos.y <= startpos.y) || (pos.x >= endpos.x || pos.y >= endpos.y))
        return false;
    
    return true;
}

void RE_InitFrameData(void)
{
    frame.indices = (uint32_t *)Hunk_Alloc(sizeof(uint32_t) * frame.maxIndices, "frameIndices", h_low);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < frame.maxIndices; i += 6) {
        frame.indices[i + 0] = offset + 0;
        frame.indices[i + 1] = offset + 1;
        frame.indices[i + 2] = offset + 2;

        frame.indices[i + 3] = offset + 2;
        frame.indices[i + 4] = offset + 3;
        frame.indices[i + 5] = offset + 0;

        offset += 4;
    }

    frame.spriteCache = RGL_InitCache(NULL, RENDER_MAX_VERTICES, NULL, RENDER_MAX_INDICES, GL_UNSIGNED_INT);
    frame.spriteShader = R_CreateShader("gamedata/sprite.glsl", "spriteShader");
    frame.sprSheet = CONSTRUCT(SpriteSheet, "spriteSheet", "NMTEXSHEET", glm::vec2(16.0f, 16.0f), glm::vec2(256, 256), NUMSPRITES);

    frame.pintVerts = (vertex_t *)Hunk_Alloc(sizeof(vertex_t) * (64 * 24 * 4), "pintVerts", h_low);
    frame.pintCache = RGL_InitCache(NULL, MAP_MAX_Y * MAP_MAX_X * 4, NULL, MAP_MAX_Y * MAP_MAX_X * 6, GL_UNSIGNED_INT);

    RGL_ResetVertexData(frame.spriteCache);
    RGL_ResetVertexData(frame.pintCache);
    RGL_ResetIndexData(frame.spriteCache);
    RGL_ResetIndexData(frame.pintCache);

    frame.sprSheet->AddSprite(SPR_SKYBOX, { 1, 0 });
    frame.sprSheet->AddSprite(SPR_ROCK, { 0, 0 });
    frame.sprSheet->AddSprite(SPR_PLAYR, { 2, 0 });
}


void RE_CmdDrawSprite(sprite_t spr, const glm::vec2& pos, const glm::vec2& size)
{
    frame.sprSheet->DrawSprite(spr, frame.spriteCache, pos, size);
    frame.numSprites++;
}

static void RE_CmdDrawPint(const glm::vec2& pos, sprite_t spr)
{
    const glm::vec2* coords = frame.sprSheet->GetSpriteCoords(SPR_ROCK);
    const glm::vec4 positions[] = {
        glm::vec4( 0.5f,  0.5f, 0.0f, 1.0f),
        glm::vec4( 0.5f, -0.5f, 0.0f, 1.0f),
        glm::vec4(-0.5f, -0.5f, 0.0f, 1.0f),
        glm::vec4(-0.5f,  0.5f, 0.0f, 1.0f),
    };
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, pos.y, 0.0f));
//                        * glm::scale(glm::mat4(1.0f), glm::vec3(0.0625f, 0.0625f, 0.0f)); // 16x16 texture sprite pints
    const glm::mat4 mvp = renderer->camera.GetProjection() * renderer->camera.GetViewMatrix() * model;
    vertex_t verts[4];

    for (uint32_t i = 0; i < 4; ++i) {
        verts[i].pos = mvp * positions[i];
        verts[i].texcoords = coords[i];
        verts[i].color = glm::vec4(0.0f);
    }
    RGL_SwapVertexData(verts, 4, frame.pintCache);
    frame.numPints++;
}

static void R_GetMapBuffer(void)
{
    EASY_FUNCTION();
}

void RE_DrawPints(void)
{
    EASY_FUNCTION();
    R_GetMapBuffer();
    sprite_t spr;
    glm::mat4 mvp, model;
    const glm::ivec2 start = glm::ivec2(
        Game::Get()->cameraPos.x - (width / 2),
        Game::Get()->cameraPos.y - (height / 2)
    );
    const glm::ivec2 end = glm::ivec2(
        Game::Get()->cameraPos.x + (width / 2),
        Game::Get()->cameraPos.y + (height / 2)
    );
    const uint32_t from = width * 0.25f;
    const float pintVertexWidth = 0.5f;
    const glm::vec4 positions[] = {
        glm::vec4( pintVertexWidth,  pintVertexWidth, 0.0f, 1.0f),
        glm::vec4( pintVertexWidth, -pintVertexWidth, 0.0f, 1.0f),
        glm::vec4(-pintVertexWidth, -pintVertexWidth, 0.0f, 1.0f),
        glm::vec4(-pintVertexWidth,  pintVertexWidth, 0.0f, 1.0f)
    };
    vertex_t *verts = frame.pintVerts;
#if 0
    struct RENDER_RECT
    {
        uint64_t left;
        uint64_t top;
        uint64_t right;
        uint64_t bottom;
    };

    RENDER_RECT renderLoc = {0, 0, 16, 16};

    int32_t startCol = Game::Get()->cameraPos.x / 16;
    int32_t startRow = Game::Get()->cameraPos.y / 16;

    int32_t visibleTilesY = (r_screenheight.i / 16);
    int32_t visibleTilesX = (r_screenwidth.i / 16);

    if (r_screenwidth.i % 16)
        visibleTilesX++;
    if (r_screenheight.i % 16)
        visibleTilesY++;
    
    int32_t endCol = startCol + visibleTilesX;
    int32_t endRow = startRow + visibleTilesY;

    int32_t y, x, l;

    x = Game::Get()->cameraPos.x % 16;
    y = Game::Get()->cameraPos.y % 16;

    if (!x) {
        endCol--;
    }
    else {
        renderLoc.right -= x;
        renderLoc.left -= x;
    }

    if (!y) {
        endRow--;
    }
    else {
        renderLoc.top -= y;
        renderLoc.bottom -= y;
    }

    if (endCol >= MAP_MAX_X)
        endCol = MAP_MAX_X;
    if (endRow >= MAP_MAX_Y)
        endRow = MAP_MAX_Y;
    
#endif
    const GDRMap& mapData = *Game::Get()->c_map;
    uint32_t y2, x2;
    x2 = 0;
    y2 = 0;

    for (int32_t x = start.x; x <= end.x; x++) {
        for (int32_t y = start.y; y <= end.y; y++) {
            model = glm::translate(glm::mat4(1.0f), glm::vec3(x2 - from / pintVertexWidth, height - y2, 0.0f));
            mvp = renderer->camera.GetProjection() * renderer->camera.GetViewMatrix() * model;

            for (uint32_t i = 0; i < 4; ++i) {
                verts->pos = mvp * positions[i];
                verts->texcoords = frame.sprSheet->GetSpriteCoords(mapData[y][x])[i];
                verts->color = glm::vec4(0.0f);
                verts++;
            }
            x2++;
        }
        y2++;
    }
}

void RE_BeginFrame(void)
{
    EASY_FUNCTION();
#ifdef _NOMAD_DEBUG
    Hunk_Check();
#endif
    RE_SetDefaultState();
    renderer->camera.CalculateViewMatrix();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, r_screenwidth.i, r_screenheight.i);
}

void RE_EndFrame(void)
{
    EASY_FUNCTION();
    R_BindShader(frame.spriteShader);
    R_SetMat4(frame.spriteShader, "u_ViewProjection", renderer->camera.GetVPM());
    frame.sprSheet->BindSheet();
    
    RGL_ResetVertexData(frame.pintCache);
    RGL_ResetIndexData(frame.pintCache);
    RE_DrawPints();
    RGL_SwapIndexData(frame.indices, width * height * 6, frame.pintCache);
    RGL_SwapVertexData(frame.pintVerts, width * height * 4, frame.pintCache);
    RGL_DrawCache(frame.pintCache);

    // draw all the sprites over the pints
    RGL_ResetIndexData(frame.spriteCache);
    RGL_SwapIndexData(frame.indices, frame.numSprites * 6, frame.spriteCache);
    RGL_DrawCache(frame.spriteCache);

    frame.numSprites = 0;
    frame.numPints = 0;
    frame.sprSheet->UnbindSheet();
    R_UnbindShader();
}


void R_BindTexture(const texture_t* texture, uint32_t slot)
{
    if (renderer->textureid == texture->id) {
        return;
    }
    else if (renderer->textureid) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    renderer->textureid = texture->id;
    glActiveTexture(GL_TEXTURE0+slot);
    glBindTexture(GL_TEXTURE_2D, texture->id);
}

void R_UnbindTexture(void)
{
    if (renderer->textureid == 0) {
        return;
    }
    renderer->textureid = 0;
    glBindTexture(GL_TEXTURE_2D, 0);
}

void R_BindShader(const shader_t* shader)
{
    if (renderer->shaderid == shader->id) {
        return;
    }
    renderer->shaderid = shader->id;
    glUseProgram(shader->id);
}

void R_UnbindShader(void)
{
    if (renderer->shaderid == 0) {
        return;
    }
    glUseProgram(0);
    renderer->shaderid = 0;
}
