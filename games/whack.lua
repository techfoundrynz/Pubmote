-- pubmote-game {"api":1,"id":"whack","title":"Whack-a-Baguette","version":"1.0.0"}
local r,t=game.rect,game.text
local s
local function bread(kind) return kind==0 or kind==1 end
local function mult() return math.min(4,1+math.floor(s.combo/5)) end
local function eased(base,minimum) return base-math.floor((base-minimum)*s.hits/(s.hits+40)) end
local function popup(row,text,tone) s.popup={row=row,text=text,tone=tone,time=.5} end
local function reset(playing)
 s={state=playing and 1 or 0,best=s and s.best or 0,score=0,hits=0,combo=0,lives=3,target=3,spawn=.5,
    holes={},pose=0,pose_time=0,guard=0,popup={time=0}}
 for i=1,5 do s.holes[i]={phase=0,kind=0,pop=0,run=0,step=0,style=0,bake=0,stache=false} end
end
function init(best) s={best=best};reset(false) end
local function lose()
 s.combo=0;s.lives=math.max(0,s.lives-1)
 if s.lives==0 then
  s.state=2;s.guard=.7;s.pose=0;s.best=math.max(s.best,s.score);game.save_score(s.best);game.tone(262,240);game.haptic(1)
 end
end
local function gain(count) s.lives=math.min(5,s.lives+count);game.tone(880,100);game.haptic(1) end
local function spawn()
 local idle,active={},0
 for i,h in ipairs(s.holes) do if h.phase==0 then idle[#idle+1]=i elseif h.phase~=4 then active=active+1 end end
 if #idle==0 or active>=1+(s.hits>=15 and 1 or 0)+(s.hits>=45 and 1 or 0) then return end
 local h=s.holes[idle[math.random(#idle)]];local roll=math.random(0,99)
 if s.lives<5 and roll<1 then h.kind=4
 elseif s.lives<5 and roll<6 then h.kind=3
 elseif roll<14 then h.kind=1
 elseif s.hits>=8 and roll<26 then h.kind=2
 else h.kind=0 end
 h.style=h.kind==3 and math.random(0,3) or (math.random(100)<=40 and 0 or math.random(1,5))
 h.bake=math.random(0,2);h.stache=math.random(100)<=30
 h.phase=1;h.pop=0;h.run=0;h.step=0
 h.timer=(bread(h.kind) or h.kind==2) and eased(1200,550)/1000 or .9
end
local function hit()
 if s.state~=1 then if s.guard<=0 then reset(true) end;return end
 local h=s.holes[s.target]
 local hittable=((h.phase==1 or h.phase==2 or h.phase==3) and h.pop>=.35) or (h.phase==5 and math.abs(h.run)<9)
 s.pose=hittable and 1 or 2;s.pose_time=.09
 if not hittable then s.combo=0;game.tone(220,30);game.haptic(0);return end
 h.phase=4;h.timer=.18;h.step=0
 if h.kind==2 then popup(s.target,({"NON!","MON DIEU!","SACRE BLEU!"})[math.random(3)],1);game.tone(130,160);lose();return end
 if h.kind==3 or h.kind==4 then local n=h.kind==4 and 3 or 1;popup(s.target,"+"..n.." LIFE",2);gain(n);return end
 s.hits=s.hits+1;s.combo=s.combo+1
 local points=(h.kind==1 and 5 or 1)*mult();s.score=s.score+points
 if h.kind~=1 and s.combo%5==0 then popup(s.target,({"MAGNIFIQUE!","FANTASTIQUE!"})[math.random(2)],0)
 elseif h.kind~=1 and math.random(100)<=25 then popup(s.target,({"OUI!","BRAVO!"})[math.random(2)],0)
 else popup(s.target,"+"..points,0) end
 game.tone(h.kind==1 and 880 or 523,70);game.haptic(h.kind==1 and 1 or 0)
 if s.combo==15 and s.lives<5 then popup(s.target,"+1 LIFE",2);gain(1) end
end
function event(kind,x,y)
 if kind==0 then hit()
 elseif kind==5 then s.target=math.max(1,math.min(5,math.floor((y-24)/12+.5)+1));hit() end
end
function update(dt,js_x,js_y,joystick)
 s.guard=math.max(0,s.guard-dt)
 if s.state~=1 then return end
 if joystick then
  local pos=(1-math.max(-1,math.min(1,js_y)))*2+1
  if math.abs(pos-s.target)>.6 then s.target=math.max(1,math.min(5,math.floor(pos+.5))) end
 end
 s.pose_time=s.pose_time-dt
 if s.pose_time<=0 then if s.pose==1 or s.pose==2 then s.pose=3;s.pose_time=.07 else s.pose=0 end end
 s.popup.time=math.max(0,s.popup.time-dt)
 for i,h in ipairs(s.holes) do
  local escaped=false
  if h.phase==1 then h.pop=math.min(1,h.pop+5*dt);if h.pop==1 then h.phase=2 end
  elseif h.phase==2 then
   h.timer=h.timer-dt
   if h.timer<=0 then
    local flee=math.min(25,8+math.floor(s.hits/4))*(h.kind==1 and 2 or 1)
    h.phase=bread(h.kind) and math.random(100)<=flee and 5 or 3
   end
  elseif h.phase==5 then
   h.run=h.run+(i%2==0 and 1 or -1)*60*dt;h.step=(h.step+dt)%.09
   if math.abs(h.run)>=48 then h.phase=0;h.pop=0;h.run=0;escaped=true;popup(i,"AU REVOIR!",1) end
  elseif h.phase==3 then
   h.pop=math.max(0,h.pop-5*dt)
   if h.pop==0 then h.phase=0;if bread(h.kind) then escaped=true;popup(i,"TROP TARD!",1) end end
  elseif h.phase==4 then
   if h.timer>0 then h.timer=h.timer-dt else h.pop=math.max(0,h.pop-7*dt);if h.pop==0 then h.phase=0;h.run=0 end end
  end
  if escaped then game.tone(330,90);game.haptic(0);lose();if s.state~=1 then return end end
 end
 s.spawn=s.spawn-dt;if s.spawn<=0 then spawn();s.spawn=(eased(1000,420)+math.random(0,299))/1000 end
end
local function scenery()
 r(0,0,100,100,0x10251b)
 for i=0,6 do r(0,i*16+8,100,8,0x1a3721) end
 r(70,8,6,6,0xf3e9c6,3)
 for i=1,9 do r((i*23)%100,(i*7)%20,.6,.6,0xfff6d0) end
 -- Eiffel Tower lattice is package drawing data, not generated Slint code.
 for i=0,11 do
  local y=24+i*3;local spread=i*.55
  r(14-spread,y,1.2,3.6,0xa9823a);r(14+spread,y,1.2,3.6,0xa9823a)
  if i%2==0 then r(14-spread,y,spread*2+1.2,.6,0xf0cd68) end
 end
 r(13.8,20,1.6,8,0xf0cd68);r(6,61,18,2,0xa9823a)
 r(78,40,16,12,0x9b9787);r(83,44,6,8,0x10251b,3)
 for _,p in ipairs({{85,22},{14,68}}) do
  r(p[1],p[2],1,14,0x38494a);r(p[1]-2,p[2],5,4,0xffcf69,1)
 end
 for i=1,8 do local x=(i*17)%90+5;local y=77+(i%3)*3;r(x,y,1,3,0x498443);r(x-1,y,3,1.5,i%2==0 and 0xef6161 or 0xe9e6c7,1) end
end
function draw()
 scenery()
 t(20,3,60,"SCORE "..s.score,0xffd166,14)
 t(17,11,66,"LIVES "..s.lives.."    x"..mult(),0xf3e9c6,11)
 for i,h in ipairs(s.holes) do
  local x=(i%2==0 and 59 or 41);local y=26+(i-1)*12
  r(x-9,y+3,18,5,i==s.target and 0xad8739 or 0x08150d,3)
  if h.pop>0 then
   local bx=x+h.run;local by=y+5-h.pop*11
   if h.kind==3 or h.kind==4 then
    r(bx-2,by+3,4,8,h.kind==4 and 0xffd166 or 0x285337,1);r(bx-.9,by,.0+1.8,4,0x497550,.5)
    r(bx-1.7,by+6,3.4,3,0xefe2b9);r(bx-.9,by,1.8,1,0x8e3843)
   elseif h.kind==2 then
    r(bx-5,by+4,10,5,0xbc7838,2.5);r(bx-3,by+3,6,5,0xe9b76b,2.5);r(bx-2,by+6,4,4,0x1a3721,2)
   else
    local crust=h.kind==1 and 0xffd166 or ({0xe9c08a,0xca9457,0x986033})[h.bake+1]
    r(bx-2.8,by,5.6,11,crust,2.8)
    for n=1,3 do r(bx-1.5,by+n*2,3,.7,0xf5d9a8,.3) end
    local berets={0x202431,0x9d3034,0x384a7b,0x526542,0x976aa2,0x785331}
    r(bx-3.6,by,7,2,berets[h.style+1],1);r(bx-.4,by-.5,.8,1,berets[h.style+1])
    r(bx-1.5,by+4,.6,.8,0x161915);r(bx+1,by+4,.6,.8,0x161915)
    if h.stache then r(bx-1.4,by+5,2.8,.6,0x25251d,.3) end
    if h.phase==5 then r(bx-3,by+10,2,2,0x25251d,1);r(bx+1,by+10+(h.step>.045 and 1 or 0),2,2,0x25251d,1) end
   end
   if h.phase==4 then t(bx-6,by-4,12,"BONK",0xffffff,10) end
  end
 end
 local hx=(s.target%2==0 and 59 or 41)+10;local hy=26+(s.target-1)*12
 r(hx,hy-4+(s.pose==1 and 4 or 0),2,13,0x9d754c,1);r(hx-3,hy-6+(s.pose==1 and 4 or 0),8,5,0xbd965f,1)
 if s.popup.time>0 then t(15,17+(s.popup.row-1)*12,70,s.popup.text,s.popup.tone==1 and 0xff7777 or 0xffd166,11) end
 if s.state~=1 then
  r(10,25,80,49,0x172a25,4)
  t(12,28,76,"WHACK-A-BAGUETTE",0xffd166,14)
  t(12,39,76,s.state==0 and "Stick aims, button whacks" or "SCORE "..s.score.." / BEST "..s.best,0xffffff,11)
  t(12,48,76,"Gold: 5 points. Spare croissants",0xcad3b4,10)
  t(12,56,76,"Wine restores lives. Tap holes to hit",0xcad3b4,10)
  t(12,65,76,"TAP OR PRESS TO START",0xffd166,10)
 end
end
function inspect() return s end
