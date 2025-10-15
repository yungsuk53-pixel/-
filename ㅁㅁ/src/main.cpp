#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <queue>
#include <fstream>
#include <sstream>
#include <optional>
#include <cmath>
#include <algorithm>
#include <random>
#include <functional>
#include <cstdio>

// ========== 유틸 ==========
template <typename T>
T clamp(T v, T lo, T hi) { return std::max(lo, std::min(v, hi)); }

float lengthSq(const sf::Vector2f& v){ return v.x*v.x + v.y*v.y; }
float lerp(float a,float b,float t){ return a + (b-a)*t; }

// ========== 엔티티 / 컴포넌트 ==========
using Entity = uint32_t;

struct EntityManager {
    Entity nextId = 1;
    std::vector<Entity> freeIds;
    Entity create() {
        if (!freeIds.empty()) { Entity id = freeIds.back(); freeIds.pop_back(); return id; }
        return nextId++;
    }
    void destroy(Entity e) { freeIds.push_back(e); }
};

struct Transform {
    sf::Vector2f pos{0,0};
    sf::Vector2f vel{0,0};
    sf::Vector2f size{24,28}; // 충돌 박스 크기
};

struct Renderable {
    bool useSprite = false;
    sf::Sprite sprite;
    sf::RectangleShape rect;
    int zIndex = 0;
};

struct Collider {
    bool solid = true;
};

struct PlayerTag{};
struct NPCTag{};

struct Interactable {
    float radius = 40.f;
    std::function<void(Entity)> onInteract;
};

struct InventoryItem {
    std::string id;
    int count = 0;
};

struct Inventory {
    int maxSlots = 8;
    std::vector<InventoryItem> items;
    void add(const std::string& id, int count=1){
        for(auto& it : items){ if(it.id==id){ it.count+=count; return; } }
        if((int)items.size()<maxSlots) items.push_back({id,count});
    }
};

// ========== 리소스 매니저 ==========
struct ResourceManager {
    std::unordered_map<std::string, sf::Texture> textures;
    std::unordered_map<std::string, sf::Font> fonts;
    std::unordered_map<std::string, sf::SoundBuffer> sounds;

    const sf::Texture* getTexture(const std::string& path){
        auto it = textures.find(path);
        if(it!=textures.end()) return &it->second;
        sf::Texture tex;
        if(!tex.loadFromFile(path)){
            std::cerr<<"[WARN] Texture not found: "<<path<<"\n";
            return nullptr;
        }
        textures[path] = std::move(tex);
        return &textures[path];
    }

    const sf::Font* getFont(const std::string& path){
        auto it = fonts.find(path);
        if(it!=fonts.end()) return &it->second;
        sf::Font font;
        if(!font.loadFromFile(path)){
            std::cerr<<"[WARN] Font not found: "<<path<<"\n";
            return nullptr;
        }
        fonts[path] = std::move(font);
        return &fonts[path];
    }

    const sf::SoundBuffer* getSound(const std::string& path){
        auto it = sounds.find(path);
        if(it!=sounds.end()) return &it->second;
        sf::SoundBuffer buf;
        if(!buf.loadFromFile(path)){
            std::cerr<<"[WARN] Sound not found: "<<path<<"\n";
            return nullptr;
        }
        sounds[path] = std::move(buf);
        return &sounds[path];
    }
};

// ========== 타일맵 ==========
struct TileMap {
    int tileSize = 32;
    int width = 0, height = 0;
    std::vector<char> tiles; // '.', '#', 'w', 'F', ' ' ...
    sf::VertexArray vao;
    std::vector<bool> solid;

    // 샘플 맵(직접 수정 가능)
    const std::vector<std::string> MAP = {
        "############################",
        "#..........w......F....####",
        "#..P.......w.............##",
        "#..........w....N..........",
        "#..........w...............",
        "#..........w....F..........",
        "#..............#####.......",
        "#.....F....................",
        "#....................F.....",
        "#...........w..............",
        "#######.....w..............",
        "######......w..............",
        "#####.......w..............",
        "####.......................",
        "###.....................###",
        "############################"
    };

    sf::Vector2i playerSpawn{64,64};
    sf::Vector2i npcSpawn{200, 200};
    std::vector<sf::Vector2i> fruitSpots; // 'F' 타일 위치

    void build() {
        height = (int)MAP.size();
        width = MAP.empty()?0:(int)MAP[0].size();
        tiles.resize(width*height);
        solid.assign(width*height,false);
        fruitSpots.clear();

        for(int y=0;y<height;++y){
            for(int x=0;x<width;++x){
                char c = MAP[y][x];
                tiles[y*width+x] = c;
                if(c=='#' || c=='w' || c=='F') solid[y*width+x] = true; // F는 나무/과일 나무로 간주(충돌)
                if(c=='P') { playerSpawn = { x*tileSize + tileSize/2, y*tileSize + tileSize/2 }; }
                if(c=='N') { npcSpawn = { x*tileSize + tileSize/2, y*tileSize + tileSize/2 }; }
                if(c=='F') { fruitSpots.push_back({x,y}); }
            }
        }

        vao.setPrimitiveType(sf::Quads);
        vao.resize(width*height*4);

        for(int y=0;y<height;++y){
            for(int x=0;x<width;++x){
                sf::Vertex* quad = &vao[(x+y*width)*4];
                float px = (float)x*tileSize;
                float py = (float)y*tileSize;
                quad[0].position = {px, py};
                quad[1].position = {px+tileSize, py};
                quad[2].position = {px+tileSize, py+tileSize};
                quad[3].position = {px, py+tileSize};

                sf::Color col;
                char c = tiles[y*width+x];
                switch(c){
                    case '#': col = sf::Color(40,100,40); break;    // 숲(벽)
                    case '.': col = sf::Color(95,160,95); break;    // 잔디
                    case 'w': col = sf::Color(70,120,180); break;   // 물
                    case 'F': col = sf::Color(120,170,80); break;   // 과일나무
                    case 'P': col = sf::Color(95,160,95); break;
                    case 'N': col = sf::Color(95,160,95); break;
                    default: col = sf::Color(95,160,95); break;
                }
                quad[0].color = quad[1].color = quad[2].color = quad[3].color = col;
            }
        }
    }

    bool isSolidAt(float px, float py) const {
        int x = (int)std::floor(px / tileSize);
        int y = (int)std::floor(py / tileSize);
        if(x<0||y<0||x>=width||y>=height) return true; // 맵 밖은 충돌 처리
        return solid[y*width+x];
    }

    void draw(sf::RenderTarget& target) const {
        target.draw(vao);
    }
};

// ========== 대화 시스템 ==========
struct DialogueBox {
    const sf::Font* font = nullptr;
    sf::RectangleShape panel;
    sf::Text text;
    std::queue<std::string> lines;
    std::string current;
    std::size_t showCount = 0;
    float timer = 0.f;
    float speed = 40.f; // 글자/초
    bool active = false;

    void init(const sf::Font* fnt, const sf::Vector2u& winSize){
        font = fnt;
        panel.setFillColor(sf::Color(0,0,0,180));
        panel.setSize(sf::Vector2f((float)winSize.x-40.f, 120.f));
        panel.setPosition(20.f, (float)winSize.y - 140.f);

        if(font) text.setFont(*font);
        text.setCharacterSize(20);
        text.setFillColor(sf::Color::White);
        text.setPosition(panel.getPosition()+sf::Vector2f(16,12));
    }

    void setFont(const sf::Font* fnt){
        font = fnt;
        if(font) text.setFont(*font);
    }

    void push(const std::string& line){
        lines.push(line);
    }

    void open(){
        if(!lines.empty()){
            active = true; current = lines.front(); lines.pop();
            showCount = 0; timer = 0.f;
        }
    }

    void update(float dt){
        if(!active) return;
        timer += dt;
        std::size_t target = (std::size_t)std::floor(timer*speed);
        if(target > showCount) showCount = std::min(target, current.size());
        text.setString(current.substr(0, showCount));
    }

    void next(){
        if(!active) return;
        if(showCount < current.size()){
            // 스킵하여 전체 표시
            showCount = current.size();
            text.setString(current);
            return;
        }
        if(!lines.empty()){
            current = lines.front(); lines.pop();
            showCount = 0; timer = 0.f;
        } else {
            active = false;
        }
    }

    void draw(sf::RenderTarget& target){
        if(!active) return;
        target.draw(panel);
        if(font) target.draw(text);
    }
};

// ========== 저장/로드 ==========
struct SaveLoad {
    std::string path = "save.txt";
    bool save(const Transform& playerTf, const Inventory& inv, float timeOfDay){
        std::ofstream ofs(path);
        if(!ofs) return false;
        ofs<<"player_x="<<playerTf.pos.x<<"\n";
        ofs<<"player_y="<<playerTf.pos.y<<"\n";
        ofs<<"time="<<timeOfDay<<"\n";
        int n = (int)inv.items.size();
        ofs<<"inv="<<n<<"\n";
        for(int i=0;i<n;++i){
            ofs<<"item"<<i<<"="<<inv.items[i].id<<","<<inv.items[i].count<<"\n";
        }
        return true;
    }
    bool load(Transform& playerTf, Inventory& inv, float& timeOfDay){
        std::ifstream ifs(path);
        if(!ifs) return false;
        std::string line;
        std::unordered_map<std::string,std::string> kv;
        while(std::getline(ifs,line)){
            auto p = line.find('=');
            if(p!=std::string::npos){
                kv[line.substr(0,p)] = line.substr(p+1);
            }
        }
        try{
            playerTf.pos.x = std::stof(kv.at("player_x"));
            playerTf.pos.y = std::stof(kv.at("player_y"));
            timeOfDay = std::stof(kv.at("time"));
            int n = std::stoi(kv.at("inv"));
            inv.items.clear();
            for(int i=0;i<n;++i){
                std::string key = "item"+std::to_string(i);
                if(kv.count(key)){
                    auto s = kv[key];
                    auto cpos = s.find(',');
                    std::string id = s.substr(0,cpos);
                    int count = std::stoi(s.substr(cpos+1));
                    inv.items.push_back({id,count});
                }
            }
        } catch(...) { return false; }
        return true;
    }
};

// ========== 게임 본체 ==========
struct Game {
    sf::RenderWindow window;
    ResourceManager res;
    TileMap map;
    EntityManager em;

    // 컴포넌트 스토리지
    std::unordered_map<Entity, Transform> transforms;
    std::unordered_map<Entity, Renderable> renderables;
    std::unordered_map<Entity, Collider> colliders;
    std::unordered_map<Entity, PlayerTag> players;
    std::unordered_map<Entity, NPCTag> npcs;
    std::unordered_map<Entity, Interactable> interactables;
    std::unordered_map<Entity, Inventory> inventories;

    // 입력 상태
    struct Input { bool up=false,down=false,left=false,right=false,interact=false; } input;
    bool prevInteract = false;

    // 시스템/기타
    DialogueBox dialog;
    SaveLoad saveload;

    // 시간/주기
    float timeOfDay = 8.0f*60.0f; // 분 단위(8시 시작)
    float timeSpeed = 12.0f; // 1초당 12분 진행(120x 가속)

    // 플레이어/NPC 핸들
    Entity player = 0;
    Entity npc1 = 0;

    // RNG
    std::mt19937 rng{std::random_device{}()};

    Game(){
        sf::VideoMode vm(1024, 768);
        window.create(vm, "CozyVillage (동물의 숲 느낌의 오리지널 샘플)", sf::Style::Default);
        window.setFramerateLimit(60);

        map.build();

        // 폰트 로드(선택)
        const sf::Font* font = res.getFont("assets/fonts/NotoSans-Regular.ttf");
        dialog.init(font, window.getSize());

        // 엔티티 생성
        createPlayer();
        createNPC();
        createFruitInteractables();

        // 저장 데이터 로드
        float loadedTime= timeOfDay;
        if(saveload.load(transforms[player], inventories[player], loadedTime)){
            timeOfDay = loadedTime;
            std::cout<<"[INFO] Save loaded.\n";
        } else {
            // 초기 위치를 스폰으로
            transforms[player].pos = sf::Vector2f((float)map.playerSpawn.x, (float)map.playerSpawn.y);
        }
    }

    void createPlayer(){
        player = em.create();
        Transform tf;
        tf.pos = sf::Vector2f((float)map.playerSpawn.x, (float)map.playerSpawn.y);
        tf.size = {22,26};
        transforms[player] = tf;

        Renderable rd;
        rd.rect.setSize({24,28});
        rd.rect.setOrigin(rd.rect.getSize()/2.f);
        rd.rect.setFillColor(sf::Color(230,220,120)); // 플레이어 바디
        rd.zIndex = 1;
        renderables[player] = rd;

        colliders[player] = Collider{true};
        players[player] = PlayerTag{};
        inventories[player] = Inventory{};
    }

    void createNPC(){
        npc1 = em.create();
        Transform tf;
        tf.pos = sf::Vector2f((float)map.npcSpawn.x, (float)map.npcSpawn.y);
        tf.size = {22,26};
        transforms[npc1] = tf;

        Renderable rd;
        rd.rect.setSize({24,28});
        rd.rect.setOrigin(rd.rect.getSize()/2.f);
        rd.rect.setFillColor(sf::Color(180,120,220)); // NPC 색
        rd.zIndex = 1;
        renderables[npc1] = rd;

        colliders[npc1] = Collider{true};
        npcs[npc1] = NPCTag{};

        // 상호작용(대화)
        Interactable it;
        it.radius = 48.f;
        it.onInteract = [this](Entity){
            if(!dialog.active){
                dialog.push("안녕! 이 마을에 온 걸 환영해.");
                dialog.push("과일 나무에서 열매를 흔들어 봐. 인벤토리에 담길 거야!");
                dialog.push("밤이 오면 분위기가 달라져. 산책하기 좋지?");
                dialog.open();
            } else {
                dialog.next();
            }
        };
        interactables[npc1] = it;
    }

    void createFruitInteractables(){
        for(auto& p : map.fruitSpots){
            Entity e = em.create();
            Transform tf;
            tf.pos = sf::Vector2f((p.x+0.5f)*map.tileSize, (p.y+0.5f)*map.tileSize);
            tf.size = {28,28};
            transforms[e] = tf;

            Renderable rd;
            rd.rect.setSize({28,28});
            rd.rect.setOrigin(rd.rect.getSize()/2.f);
            rd.rect.setFillColor(sf::Color(140,200,90));
            rd.zIndex = 1;
            renderables[e] = rd;

            colliders[e] = Collider{true};

            Interactable it;
            it.radius = 40.f;
            it.onInteract = [this, e](Entity){
                // 랜덤으로 과일 획득
                std::uniform_int_distribution<int> dist(0,2);
                std::string fruits[3] = {"사과","배","복숭아"};
                std::string item = fruits[dist(rng)];
                inventories[player].add(item, 1);
                if(!dialog.active){
                    dialog.push(std::string("나무에서 ") + item + " 를(을) 주웠다!");
                    dialog.open();
                } else {
                    dialog.next();
                }
            };
            interactables[e] = it;
        }
    }

    void run(){
        sf::Clock clock;
        while(window.isOpen()){
            float dt = clock.restart().asSeconds();
            handleEvents();
            update(dt);
            render();
        }
    }

    void handleEvents(){
        input = {};
        sf::Event ev;
        while(window.pollEvent(ev)){
            if(ev.type == sf::Event::Closed) {
                shutdownSave();
                window.close();
            }
            if(ev.type == sf::Event::KeyPressed){
                if(ev.key.code == sf::Keyboard::Escape){
                    shutdownSave();
                    window.close();
                }
            }
        }
        // 연속 입력
        input.left  = sf::Keyboard::isKeyPressed(sf::Keyboard::A) || sf::Keyboard::isKeyPressed(sf::Keyboard::Left);
        input.right = sf::Keyboard::isKeyPressed(sf::Keyboard::D) || sf::Keyboard::isKeyPressed(sf::Keyboard::Right);
        input.up    = sf::Keyboard::isKeyPressed(sf::Keyboard::W) || sf::Keyboard::isKeyPressed(sf::Keyboard::Up);
        input.down  = sf::Keyboard::isKeyPressed(sf::Keyboard::S) || sf::Keyboard::isKeyPressed(sf::Keyboard::Down);
        input.interact = sf::Keyboard::isKeyPressed(sf::Keyboard::E);
    }

    void update(float dt){
        // 시간 진행(대화 중에도 진행)
        timeOfDay += dt * timeSpeed;
        if(timeOfDay >= 24.f*60.f) timeOfDay -= 24.f*60.f;

        // 대화 업데이트
        dialog.update(dt);

        // 플레이어 이동 입력(대화 중엔 못 움직이도록 막아도 OK)
        auto& pTf = transforms[player];
        sf::Vector2f move(0,0);
        if(!dialog.active){
            if(input.left) move.x -= 1;
            if(input.right) move.x += 1;
            if(input.up) move.y -= 1;
            if(input.down) move.y += 1;
        }
        float speed = 120.f;
        if(move.x!=0 || move.y!=0){
            float invLen = 1.0f / std::sqrt(move.x*move.x + move.y*move.y);
            move *= invLen;
        }
        pTf.vel = move * speed;

        // NPC 간단한 흔들림/유휴
        if(npcs.count(npc1)){
            auto& nTf = transforms[npc1];
            static float t = 0.f; t += dt;
            nTf.pos.x += std::sin(t*0.5f)*0.05f;
            nTf.pos.y += std::cos(t*0.6f)*0.05f;
        }

        // 이동/충돌 처리
        physicsMove(player, dt);

        // 상호작용 처리(E 키 토글)
        if(input.interact && !prevInteract){
            tryInteract();
        }
        prevInteract = input.interact;
    }

    void physicsMove(Entity e, float dt){
        auto& tf = transforms[e];
        sf::Vector2f half = tf.size*0.5f;

        // X축 이동
        float newX = tf.pos.x + tf.vel.x * dt;
        resolveAxis(newX, tf.pos.y, half, true);
        tf.pos.x = newX;

        // Y축 이동
        float newY = tf.pos.y + tf.vel.y * dt;
        resolveAxis(tf.pos.x, newY, half, false);
        tf.pos.y = newY;
    }

    void resolveAxis(float& px, float py, const sf::Vector2f& half, bool axisX){
        float left = px - half.x;
        float right = px + half.x;
        float top = py - half.y;
        float bottom = py + half.y;

        int x0 = (int)std::floor(left / map.tileSize);
        int x1 = (int)std::floor(right / map.tileSize);
        int y0 = (int)std::floor(top / map.tileSize);
        int y1 = (int)std::floor(bottom / map.tileSize);

        x0 = std::max(0, std::min(map.width-1, x0));
        x1 = std::max(0, std::min(map.width-1, x1));
        y0 = std::max(0, std::min(map.height-1, y0));
        y1 = std::max(0, std::min(map.height-1, y1));

        for(int y = y0; y<=y1; ++y){
            for(int x = x0; x<=x1; ++x){
                if(map.solid[y*map.width+x]){
                    float tx = x * map.tileSize;
                    float ty = y * map.tileSize;
                    sf::FloatRect tileRect(tx, ty, (float)map.tileSize, (float)map.tileSize);
                    sf::FloatRect aabb(left, top, right-left, bottom-top);
                    if(aabb.intersects(tileRect)){
                        if(axisX){
                            if((px) < tileRect.left) {
                                px = tileRect.left - half.x - 0.01f;
                            } else {
                                px = tileRect.left + tileRect.width + half.x + 0.01f;
                            }
                            left = px - half.x; right = px + half.x;
                            aabb.left = left; aabb.width = right-left;
                        } else {
                            if((py) < tileRect.top){
                                py = tileRect.top - half.y - 0.01f;
                            } else {
                                py = tileRect.top + tileRect.height + half.y + 0.01f;
                            }
                            top = py - half.y; bottom = py + half.y;
                            aabb.top = top; aabb.height = bottom-top;
                        }
                    }
                }
            }
        }
    }

    void tryInteract(){
        auto& pTf = transforms[player];
        float bestDist = 1e9f;
        Entity best = 0;
        for(auto& [e, it] : interactables){
            if(!transforms.count(e)) continue;
            auto& tf = transforms[e];
            sf::Vector2f d = tf.pos - pTf.pos;
            float d2 = lengthSq(d);
            if(d2 < it.radius*it.radius && d2 < bestDist){
                bestDist = d2; best = e;
            }
        }
        if(best!=0){
            auto& it = interactables[best];
            if(it.onInteract) it.onInteract(best);
        }
    }

    void render(){
        window.clear(sf::Color(120,180,120));
        map.draw(window);

        std::vector<std::pair<int,Entity>> order;
        order.reserve(renderables.size());
        for(auto& [e, r] : renderables){
            order.push_back({r.zIndex, e});
        }
        std::sort(order.begin(), order.end(), [](auto&a, auto&b){ return a.first < b.first; });

        for(auto& [z, e] : order){
            auto& r = renderables[e];
            auto& tf = transforms[e];
            if(r.useSprite){
                r.sprite.setPosition(tf.pos);
                window.draw(r.sprite);
            } else {
                r.rect.setPosition(tf.pos);
                window.draw(r.rect);
            }
        }

        drawInventoryBar();
        drawDayNightOverlay();
        dialog.draw(window);

        window.display();
    }

    void drawInventoryBar(){
        auto it = inventories.find(player);
        if(it==inventories.end()) return;
        auto& inv = it->second;

        float pad = 10.f;
        float slotSize = 48.f;
        int slots = inv.maxSlots;
        float barWidth = slots*slotSize + (slots+1)*pad;
        float x0 = (window.getSize().x - barWidth)/2.f;
        float y0 = window.getSize().y - slotSize - 20.f;

        sf::RectangleShape slot;
        slot.setSize({slotSize, slotSize});
        for(int i=0;i<slots;++i){
            slot.setPosition(x0 + pad + i*(slotSize+pad), y0);
            slot.setFillColor(sf::Color(0,0,0,100));
            slot.setOutlineThickness(2.f);
            slot.setOutlineColor(sf::Color(255,255,255,120));
            window.draw(slot);
            if(i < (int)inv.items.size()){
                sf::RectangleShape item;
                item.setSize({slotSize-12.f, slotSize-12.f});
                item.setPosition(slot.getPosition()+sf::Vector2f(6,6));
                item.setFillColor(sf::Color(230,140,70));
                window.draw(item);

                if(auto font = res.getFont("assets/fonts/NotoSans-Regular.ttf")){
                    sf::Text t;
                    t.setFont(*font);
                    t.setCharacterSize(16);
                    t.setFillColor(sf::Color::White);
                    t.setString(inv.items[i].id + " x" + std::to_string(inv.items[i].count));
                    t.setPosition(slot.getPosition().x, slot.getPosition().y - 18.f);
                    window.draw(t);
                }
            }
        }
    }

    void drawDayNightOverlay(){
        float minutes = std::fmod(timeOfDay, 24.f*60.f);
        float hour = minutes / 60.f;

        float darkness = 0.f;
        if(hour >= 21.f || hour < 5.f) darkness = 0.55f;
        else if(hour >= 18.f && hour < 21.f) darkness = lerp(0.0f, 0.55f, (hour-18.f)/3.f);
        else if(hour >= 5.f && hour < 8.f) darkness = lerp(0.55f, 0.0f, (hour-5.f)/3.f);
        else darkness = 0.0f;

        if(darkness > 0.01f){
            sf::RectangleShape overlay;
            overlay.setSize(sf::Vector2f((float)window.getSize().x, (float)window.getSize().y));
            overlay.setFillColor(sf::Color(0,0,30, (sf::Uint8)(darkness*200)));
            window.draw(overlay);
        }

        if(auto font = res.getFont("assets/fonts/NotoSans-Regular.ttf")){
            int h = (int)std::floor(hour) % 24;
            int m = (int)std::floor(minutes) % 60;
            char buf[16]; std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
            sf::Text timeText(buf, *font, 20);
            timeText.setFillColor(sf::Color::White);
            timeText.setPosition(16.f, 12.f);
            window.draw(timeText);
        }
    }

    void shutdownSave(){
        saveload.save(transforms[player], inventories[player], timeOfDay);
    }
};

int main(){
    try{
        Game game;
        game.run();
    } catch(const std::exception& e){
        std::cerr<<"Fatal: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}
