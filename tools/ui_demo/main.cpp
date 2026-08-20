// UI demo (tasks 5.5/5.6/5.9/5.10/5.11): the built-in screen kit in the
// Codex design language, rendered by the engine over a live 3D scene —
// main menu (English and Hebrew/RTL), in-game HUD with virtual controls,
// and an inventory screen built purely from data bindings, including a
// live-object view of the player character. Captures each screen (● real
// engine output) and verifies interactions route UI-first.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mge/framework/camera_controller.h"
#include "mge/framework/items.h"
#include "mge/framework/save.h"
#include "mge/graphics/primitives.h"
#include "mge/graphics/renderer.h"
#include "mge/graphics/vulkan_device.h"
#include "mge/framework/controls.h"
#include "mge/ui/ui.h"

using namespace mge;

namespace {

constexpr uint32_t kW = 1280, kH = 720;

bool savePpm(const char* path, const std::vector<uint8_t>& rgba) {
    FILE* f = fopen(path, "wb");
    if (f == nullptr) return false;
    fprintf(f, "P6\n%u %u\n255\n", kW, kH);
    for (size_t i = 0; i < rgba.size(); i += 4) fwrite(&rgba[i], 1, 3, f);
    fclose(f);
    return true;
}

TouchEvent touch(int32_t id, TouchAction action, float x, float y, int64_t t = 0) {
    TouchEvent e;
    e.pointerId = id;
    e.action = action;
    e.x = x;
    e.y = y;
    e.timestampNs = t;
    return e;
}

struct Scene {
    GpuLodMesh ground, house, stall, villager;
    std::vector<DrawItem> items;

    bool build(Renderer& renderer) {
        auto upload = [&](MeshData data, GpuLodMesh& out) {
            LodMesh m;
            m.lods.push_back(std::move(data));
            m.computeBounds();
            return renderer.uploadLodMesh(m, out);
        };
        if (!upload(makePlane(60, 60), ground)) return false;
        if (!upload(makeBox({3.2f, 2.6f, 2.8f}), house)) return false;
        if (!upload(makeBox({2.4f, 2.1f, 1.8f}), stall)) return false;
        if (!upload(makeCapsule(0.35f, 1.8f, 20, 8), villager)) return false;

        auto add = [&](const GpuLodMesh* mesh, Vec3 pos, float yaw, MaterialKind material,
                       float r, float g, float b) {
            DrawItem item;
            item.mesh = mesh;
            Transform xf;
            xf.position = pos;
            xf.rotation = Quat::fromAxisAngle({0, 1, 0}, yaw);
            item.model = xf.toMatrix();
            const float radius = mesh->bounds.extents().length();
            item.worldBounds = Aabb::fromCenterExtents(pos, {radius, radius, radius});
            item.lodReference = pos;
            item.baseColor[0] = r;
            item.baseColor[1] = g;
            item.baseColor[2] = b;
            item.material = material;
            if (material == MaterialKind::Placeholder) item.params[0] = 6.0f;
            items.push_back(item);
        };
        add(&ground, {0, 0, 0}, 0, MaterialKind::Lit, 0.42f, 0.47f, 0.36f);
        add(&house, {-5.5f, 1.3f, -7.0f}, 0.3f, MaterialKind::Lit, 0.62f, 0.55f, 0.45f);
        add(&house, {5.0f, 1.3f, -9.0f}, -0.4f, MaterialKind::Lit, 0.58f, 0.50f, 0.42f);
        add(&stall, {-1.0f, 1.05f, -5.0f}, 0.4f, MaterialKind::Placeholder, 0.85f, 0.55f, 0.18f);
        add(&villager, {0, 0.9f, 2.0f}, 0, MaterialKind::Lit, 0.30f, 0.42f, 0.58f);
        return true;
    }

    void destroy(Renderer& renderer) {
        renderer.destroyLodMesh(ground);
        renderer.destroyLodMesh(house);
        renderer.destroyLodMesh(stall);
        renderer.destroyLodMesh(villager);
    }
};

// ------------------------------- screens (task 5.5) ------------------------

void drawMenu(Ui& ui) {
    ui.screenDim();
    const float cx = kW * 0.5f;
    ui.panel({cx - 270, 90, 540, 470});
    ui.label({cx - 230, 130, 460, 60}, "demo.title", 1.6f, TextAlign::Center);
    ui.labelInk({cx - 230, 196, 460, 26}, "menu.built_on", 0.62f, TextAlign::Center,
                ui.theme().inkFaint);
    ui.button(1, {cx - 190, 250, 380, 62}, "menu.continue");
    ui.button(2, {cx - 190, 330, 380, 62}, "menu.new_game");
    ui.button(3, {cx - 190, 410, 380, 62}, "menu.settings");
}

void drawHud(Ui& ui, float yaw, float health, const Item* held, bool stickActive,
             float anchorX, float anchorY, float stickX, float stickY) {
    ui.healthBar({26, 26, 300, 22}, health);
    ui.compassStrip({kW * 0.5f - 170, 18, 340, 32}, yaw);
    ui.itemSlot({kW - 26 - 68, 20, 68, 68}, held, false);
    ui.virtualControls(stickActive, anchorX, anchorY, stickX, stickY);
    // The Phase 12 action seals, at the control scheme's own geometry.
    TouchControlScheme scheme;
    scheme.configure(kW, kH);
    const TouchButton jumpZone = scheme.jumpButton();
    const TouchButton useZone = scheme.useButton();
    ui.actionSeal(jumpZone.x, jumpZone.y, jumpZone.radius, false, "hud.jump");
    ui.actionSeal(useZone.x, useZone.y, useZone.radius, true, "hud.use");
}

void drawInventory(Ui& ui, const ItemCollection& backpack, const ItemCollection& chest,
                   int& selBackpack, int& selChest) {
    ui.screenDim();
    // Character page (live-object view renders into the gap left of center).
    ui.panel({60, 70, 330, 560});
    ui.label({80, 86, 290, 34}, "demo.character", 0.95f, TextAlign::Center);
    // (the object view itself is rendered by the renderer into this rect)
    // Backpack + chest, purely data-bound (task 5.9).
    ui.collectionView(100, {430, 70, 380, 560}, "inv.title", backpack, 5, selBackpack);
    ui.collectionView(200, {840, 70, 380, 560}, "inv.chest", chest, 5, selChest);
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outPrefix = argc > 1 ? argv[1] : "ui";

    BudgetRegistry budgets;
    VulkanDevice device(budgets);
    if (!device.init(VulkanDeviceConfig{})) return 1;
    Renderer renderer(device);
    RendererConfig config;
    config.width = kW;
    config.height = kH;
    config.lightDir[0] = -0.25f;
    config.lightDir[1] = 0.80f;
    config.lightDir[2] = 0.55f;
    if (!renderer.init(config)) return 1;

    FontAtlas font;
    if (!font.bakeEmbedded(34.0f)) {
        return 1;
    }
    if (!renderer.setUiFont(font)) return 1;

    Localization strings;
    strings.set(Language::English, "hud.jump", "Leap");
    strings.set(Language::Hebrew, "hud.jump", "לקפוץ");
    strings.set(Language::English, "hud.use", "Use");
    strings.set(Language::Hebrew, "hud.use", "להשתמש");
    strings.set(Language::English, "demo.title", "EMBERHOLD");
    strings.set(Language::Hebrew, "demo.title", "אחוזת הגחלת");
    strings.set(Language::English, "demo.character", "Aldric");
    strings.set(Language::Hebrew, "demo.character", "אלדריק");
    strings.set(Language::English, "slots.title", "Chronicles");
    strings.set(Language::Hebrew, "slots.title", "כרוניקות");

    Ui ui;
    if (!ui.init(&font, codexTheme(), &strings)) return 1;

    Scene scene;
    if (!scene.build(renderer)) return 1;

    Camera camera;
    camera.aspect = static_cast<float>(kW) / kH;
    ThirdPersonCamera cameraController;
    cameraController.update(camera, {0, 0.9f, 2.0f}, 0.0f);

    // Item collections, registered and bound by id (task 5.9).
    ItemCollection backpack, chest;
    backpack.add({assetIdFromName("item/apple"), "item.apple", 5, {0.72f, 0.22f, 0.16f, 1}});
    backpack.add({assetIdFromName("item/rope"), "item.rope", 1, {0.55f, 0.45f, 0.28f, 1}});
    backpack.add({assetIdFromName("item/torch"), "item.torch", 2, {0.85f, 0.55f, 0.18f, 1}});
    chest.add({assetIdFromName("item/coin"), "item.coin", 24, {0.78f, 0.65f, 0.25f, 1}});
    chest.add({assetIdFromName("item/sword"), "item.sword", 1, {0.62f, 0.64f, 0.70f, 1}});
    CollectionRegistry collections;
    collections.add("player.backpack", &backpack);
    collections.add("chest.tavern", &chest);
    const Item held{assetIdFromName("item/sword"), "item.sword", 1, {0.62f, 0.64f, 0.70f, 1}};

    std::vector<uint8_t> pixels(static_cast<size_t>(kW) * kH * 4);
    int failures = 0;
    auto capture = [&](const char* name, const ObjectViewDraw* views, size_t viewCount) {
        RenderStats stats;
        if (!renderer.renderFrame(camera, scene.items.data(), scene.items.size(), &stats,
                                  &ui.drawList(), views, viewCount)) {
            ++failures;
            return;
        }
        renderer.readback(pixels.data(), pixels.size());
        char path[512];
        snprintf(path, sizeof(path), "%s_%s.ppm", outPrefix.c_str(), name);
        savePpm(path, pixels);
        printf("capture %-12s: %zu ui quads\n", name, ui.drawList().quadCount());
    };

    // --- 1) Main menu, English ---
    ui.beginFrame(kW, kH);
    drawMenu(ui);
    capture("menu_en", nullptr, 0);

    // Interaction + routing check (task 5.7): tap Continue -> consumed and
    // clicked; tap the field -> falls through.
    if (ui.handleTouch(touch(0, TouchAction::Down, 100, 650))) ++failures;   // outside: fall through
    if (!ui.handleTouch(touch(0, TouchAction::Down, kW * 0.5f, 280))) ++failures;
    if (!ui.handleTouch(touch(0, TouchAction::Up, kW * 0.5f, 280))) ++failures;
    bool continueClicked = false;
    ui.beginFrame(kW, kH);
    ui.screenDim();
    const float cx = kW * 0.5f;
    ui.panel({cx - 270, 90, 540, 470});
    ui.label({cx - 230, 130, 460, 60}, "demo.title", 1.6f, TextAlign::Center);
    ui.labelInk({cx - 230, 196, 460, 26}, "menu.built_on", 0.62f, TextAlign::Center,
                ui.theme().inkFaint);
    continueClicked = ui.button(1, {cx - 190, 250, 380, 62}, "menu.continue");
    ui.button(2, {cx - 190, 330, 380, 62}, "menu.new_game");
    ui.button(3, {cx - 190, 410, 380, 62}, "menu.settings");
    if (!continueClicked) ++failures;

    // --- 2) Main menu, Hebrew (runtime language switch, mirrored layout) ---
    strings.setLanguage(Language::Hebrew);
    ui.beginFrame(kW, kH);
    drawMenu(ui);
    capture("menu_he", nullptr, 0);
    strings.setLanguage(Language::English);

    // --- 3) In-game HUD + virtual controls ---
    ui.beginFrame(kW, kH);
    drawHud(ui, 0.6f, 0.72f, &held, true, kW * 0.16f, kH * 0.74f, kW * 0.19f, kH * 0.70f);
    capture("hud", nullptr, 0);

    // --- 4) Inventory: data-bound views + live-object character view ---
    int selBackpack = 0, selChest = -1;
    ui.beginFrame(kW, kH);
    drawInventory(ui, backpack, chest, selBackpack, selChest);
    DrawItem characterItem;
    characterItem.mesh = &scene.villager;
    Transform xf;
    xf.position = {0, 0, 0};
    xf.rotation = Quat::fromAxisAngle({0, 1, 0}, 0.5f);
    characterItem.model = xf.toMatrix();
    characterItem.baseColor[0] = 0.30f;
    characterItem.baseColor[1] = 0.42f;
    characterItem.baseColor[2] = 0.58f;
    ObjectViewDraw characterView;
    characterView.camera.eye = {0.0f, 1.1f, 2.3f};
    characterView.camera.target = {0, 0.85f, 0};
    characterView.camera.aspect = 300.0f / 420.0f;
    characterView.items = &characterItem;
    characterView.count = 1;
    characterView.rect[0] = 75;
    characterView.rect[1] = 140;
    characterView.rect[2] = 300;
    characterView.rect[3] = 420;
    characterView.clear[0] = 0.867f;  // parchmentDeep behind the character
    characterView.clear[1] = 0.808f;
    characterView.clear[2] = 0.671f;
    capture("inventory", &characterView, 1);

    // --- 5) Save-slot picker (task 6.5): real slots written by SaveManager ---
    {
        std::string dir = "/tmp";
        if (const char* t = getenv("TMPDIR")) dir = t;
        SaveManager saves(dir.c_str());
        SaveSnapshot snapshotA;
        snapshotA.player.health = 0.8f;
        snapshotA.playtimeSeconds = 4.0 * 3600 + 12 * 60;
        saves.save("chronicle_1", snapshotA);
        SaveSnapshot snapshotB;
        snapshotB.playtimeSeconds = 45 * 60;
        saves.save("chronicle_2", snapshotB);

        std::vector<SaveSlotInfo> slots;
        saves.listSlots(slots);
        // Keep only our demo slots, newest first look irrelevant for capture.
        std::vector<SaveSlotInfo> shown;
        for (const SaveSlotInfo& slot : slots) {
            if (slot.name.rfind("chronicle_", 0) == 0) shown.push_back(slot);
        }

        ui.beginFrame(kW, kH);
        ui.screenDim();
        ui.panel({kW * 0.5f - 340, 70, 680, 560});
        ui.label({kW * 0.5f - 300, 100, 600, 46}, "slots.title", 1.3f, TextAlign::Center);
        float y = 180;
        uint32_t id = 500;
        for (const SaveSlotInfo& slot : shown) {
            const UiRect card{kW * 0.5f - 290, y, 580, 96};
            ui.button(id++, card, "");
            char line[128];
            snprintf(line, sizeof(line), "%s", slot.name.c_str());
            ui.strings().set(Language::English, "slots.tmp_name", line);
            ui.label({card.x + 70, card.y + 10, card.w - 90, 36}, "slots.tmp_name", 0.9f,
                     TextAlign::Left);
            const int hours = static_cast<int>(slot.playtimeSeconds / 3600);
            const int minutes = static_cast<int>(slot.playtimeSeconds / 60) % 60;
            snprintf(line, sizeof(line), "%dh %02dm%s", hours, minutes,
                     slot.valid ? "" : "  (damaged)");
            ui.strings().set(Language::English, "slots.tmp_meta", line);
            ui.labelInk({card.x + 70, card.y + 50, card.w - 90, 30}, "slots.tmp_meta", 0.68f,
                        TextAlign::Left, ui.theme().inkFaint);
            y += 116;
        }
        capture("slots", nullptr, 0);
        saves.removeSlot("chronicle_1");
        saves.removeSlot("chronicle_2");
    }

    scene.destroy(renderer);
    renderer.shutdown();
    const size_t residual = device.gpuBudgetStats().usedBytes;
    device.shutdown();

    if (failures != 0 || residual != 0) {
        fprintf(stderr, "FAIL: failures=%d residual=%zu\n", failures, residual);
        return 1;
    }
    printf("OK\n");
    return 0;
}
