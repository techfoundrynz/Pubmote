-- pubmote-game {"api":1,"id":"flappy","title":"Flappy Penguin","version":"1.0.0"}
-- All simulation and drawing live in this independently installable package.
local r, t = game.rect, game.text
local s
local function difficulty()
    s.gap = math.max(23, 26 - s.score * .1)
    s.speed = math.min(42, 32 + s.score * .3)
end
local function gap(previous)
    local lo, hi = s.gap / 2 + 9, 81 - s.gap / 2
    if previous then lo, hi = math.max(lo, previous - 20), math.min(hi, previous + 20) end
    return lo + math.random() * (hi - lo)
end
local function reset(playing)
    s = {state=playing and 1 or 0, score=0, best=s and s.best or 0, y=42, v=0, wing=0, dying=false, pillars={}}
    difficulty()
    for i=1,3 do
        local y = gap(s.last_gap)
        s.pillars[i] = {x=104+(i-1)*46, y=y, passed=false}; s.last_gap=y
    end
end
function init(best) s={best=best}; reset(false) end
local function crash()
    if not s.dying then s.dying=true; s.wing=0; game.tone(130,200); game.haptic(1) end
end
function event(kind)
    if kind==6 then return end
    if s.state~=1 then reset(true); return end
    if not s.dying then s.v=-76; s.wing=.11; game.tone(440,40) end
end
function update(dt)
    if s.state~=1 then return end
    s.wing=math.max(0,s.wing-dt)
    if not s.dying then
        for _,p in ipairs(s.pillars) do
            p.x=p.x-s.speed*dt
            if not p.passed and p.x+6<23 then
                p.passed=true; s.score=s.score+1; difficulty(); game.tone(880,60); game.haptic(0)
            end
            if p.x < -7 then p.x=p.x+138; p.y=gap(s.last_gap); s.last_gap=p.y; p.passed=false end
        end
    end
    s.v=math.min(110,s.v+210*dt); s.y=s.y+s.v*dt
    if s.y<7 then s.y=7; s.v=math.max(0,s.v) end
    if s.y>=83.4 then
        s.y=83.4; s.v=0; crash(); s.state=2
        s.best=math.max(s.best,s.score); game.save_score(s.best); return
    end
    if not s.dying then
        for _,p in ipairs(s.pillars) do
            if math.abs(p.x-26)<=9 and (s.y-2.6<p.y-s.gap/2 or s.y+2.6>p.y+s.gap/2) then crash() end
        end
    end
end
local function overlay(title, caption)
    r(12,25,76,48,0x102c40,5)
    t(14,28,72,title,0x7be4ff,14)
    t(14,40,72,"SCORE "..s.score,0xffffff,14)
    t(14,49,72,"BEST "..s.best,0xaad9e5,11)
    t(14,61,72,caption,0xffffff,10)
end
function draw()
    r(0,0,100,100,0x071a2b)
    for i=1,12 do r((i*29)%100,(i*13)%75,.5,.5,0x9fcddd) end
    -- Layered snow drifts and ice cliffs.
    r(0,75,100,25,0x356680); r(0,80,100,20,0x7dc3d8); r(0,86,100,14,0xd3f4f5)
    for _,p in ipairs(s.pillars) do
        local top,bottom=p.y-s.gap/2,p.y+s.gap/2
        r(p.x-6,0,12,top,0x6caecb); r(p.x-5,0,3,top,0xb5e8f1)
        r(p.x-7,top-2,14,2,0xe9ffff,1)
        r(p.x-6,bottom,12,86-bottom,0x6caecb); r(p.x-5,bottom,3,86-bottom,0xb5e8f1)
        r(p.x-7,bottom,14,2,0xe9ffff,1)
    end
    -- Penguin body, belly, eye, beak and animated flipper.
    r(22,s.y-3.5,8,7,0x172c3b,3)
    r(25,s.y-2,4.5,5,0xf2f8ee,2)
    r(28,s.y-2,1,1,0x071a2b,.5)
    r(29.5,s.y-.7,2.5,1.4,0xffc24c,.6)
    r(21,s.y+(s.wing>0 and 1 or -1.5),5,1.8,0x244457,1)
    r(24,s.y+3,3,1,0xffc24c,.5)
    t(25,5,50,tostring(s.score),0xffffff,28)
    if s.state==0 then overlay("FLAPPY PENGUIN","Tap, button or stick to flap") end
    if s.state==2 then overlay("SPLASH","Tap to retry") end
end
-- Test introspection is ordinary Lua data, not a privileged host API.
function inspect() return s end
