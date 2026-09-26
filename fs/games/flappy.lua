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
    return lo + game.random(10000)/10000 * (hi - lo)
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
    if not s.dying then s.dying=true; s.wing=0; game.sequence({{187,200}}); game.haptic(5) end
end
function event(kind)
    if kind==6 or kind==7 then return false end
    if s.state~=1 then reset(true); return end
    if not s.dying then s.v=-76; s.wing=.11; game.sequence({{440,40}}) end
end
function update(dt)
    if s.state~=1 then return false end
    s.wing=math.max(0,s.wing-dt)
    if not s.dying then
        for _,p in ipairs(s.pillars) do
            p.x=p.x-s.speed*dt
            if not p.passed and p.x+6<23 then
                p.passed=true; s.score=s.score+1; difficulty(); game.sequence({{659,60},{880,60}}); game.haptic(0)
            end
            if p.x < -7 then p.x=p.x+138; p.y=gap(s.last_gap); s.last_gap=p.y; p.passed=false end
        end
    end
    s.v=math.min(110,s.v+210*dt); s.y=s.y+s.v*dt
    if s.y<7 then s.y=7; s.v=math.max(0,s.v) end
    if s.y>=83.4 then
        s.y=83.4; s.v=0; crash(); s.state=2
        if s.score>s.best then s.best=s.score;game.save_score(s.best);game.haptic(2) end; return
    end
    if not s.dying then
        for _,p in ipairs(s.pillars) do
            if math.abs(p.x-26)<=9 and (s.y-2.6<p.y-s.gap/2 or s.y+2.6>p.y+s.gap/2) then crash() end
        end
    end
end
function draw()
 local width,height,square,stick,button=game.screen();local ux=width/100;local uy=height/100
 local scale=math.min(width,height)/240
 local function unit(px) return math.floor(px*scale+.5)/uy end
 local star=math.floor(2*scale+.5)/ux;local corner=math.floor(5*scale+.5)/ux
 local ew=math.floor(width*.26+.5)/ux;local eh=math.floor(height*.10+.5)/uy
 r(0,0,100,100,0x071a2b)
 for _,v in ipairs({{20,16,0x7fb6d6},{44,10,0x9fd8f2},{66,18,0x7fb6d6},{80,13,0x9fd8f2}}) do r(v[1],v[2],star,star*ux/uy,v[3]) end
 for _,p in ipairs(s.pillars) do r(p.x-6,p.y-s.gap/2-100,12,100,0x2f9dc4,corner,2/ux,0xc9f0ff) end
 for _,p in ipairs(s.pillars) do r(p.x-6,p.y+s.gap/2,12,100,0x2f9dc4,corner,2/ux,0xc9f0ff) end
 local x,y,w,h=22,s.y-3.5,8,7
 local tilt=math.max(-1,math.min(1,s.v/80));local wing=s.wing>0 and 2 or s.v>30 and 0 or 1
 local function box(xx,yy,ww,hh,c,rad,border,edge) r(x+xx,y+yy,ww,hh,c,rad or 0,border or 0,edge or 0) end
 box(w*.06,h*.18,w*.82,h*.78,0x101a26,w*.82*.46,1/ux,0x9fd8f2)
 box(w*.34,h*.40,w*.48,h*.52,0xf2fbff,w*.48*.46)
 box(w*.18,h*(.40+(wing==0 and -.10 or wing==2 and .16 or 0)),w*.22,h*.34,0x23384c,w*.22*.5)
 box(w*.56,h*(.28+tilt*.05),w*.16,w*.16*ux/uy,0xf2fbff,w*.16*.5)
 box(w*.62,h*(.31+tilt*.05),w*.08,w*.08*ux/uy,0x101a26,w*.08*.5)
 box(w*.82,h*(.42+tilt*.14),w*.20,h*.14,0xff9d2e,1/ux)
 box(w*.36,h*(.92-tilt*.08),w*.28,h*.10,0xff9d2e,1/ux)
 r(0,86,100,14,0xe8f6ff,0,2/ux,0x9fd8f2)
 t(0,8,100,tostring(s.score),0xeaf8ff,28,0,true,true)
 if s.state~=1 then
  r(0,0,100,100,0,0,0,0,217)
  local function spacer(px) return {"",10,0,false,false,unit(px)} end
  local rows
  if s.state==0 then
   rows={{"FLAPPY PENGUIN",14,0x6fd8f5,false,true},spacer(6),{"Tap to flap",10,0x9a9a9a},{"Mind the ice",10,0x9a9a9a}}
   if stick then rows[#rows+1]={"Stick flaps too",10,0x6fd8f5} end
   if button then rows[#rows+1]={"Button flaps too",10,0x6fd8f5} end
   rows[#rows+1]=spacer(8);rows[#rows+1]={"BEST "..s.best,11,0x9a9a9a};rows[#rows+1]={"TAP TO START",12,0xffffff,false,true}
  else rows={{"SPLASH",14,0xff6369,false,true},spacer(8),{"SCORE",10,0x9a9a9a},{tostring(s.score),28,0xffffff,true},{"BEST "..s.best,11,0x9a9a9a},spacer(10),{"TAP TO RETRY",12,0xffffff,false,true}} end
  local pad=(square and 24 or 36)*scale/ux
  game.column(pad,0,100-pad*2,100,unit(s.state==0 and 3 or 2),eh,rows)
 end
 game.exit((100-ew)/2,math.floor(height*.96+.5)/uy-eh,ew,eh)
end
function inspect() return s end
