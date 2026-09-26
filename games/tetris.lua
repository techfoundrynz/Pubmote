-- pubmote-game {"api":1,"id":"tetris","title":"Tetris","version":"1.0.0"}
local r,t=game.rect,game.text
local shapes={{0x0f00,0x2222,0x00f0,0x4444},{0x8e00,0x6440,0x0e20,0x44c0},{0x2e00,0x4460,0x0e80,0xc440},
 {0x6600,0x6600,0x6600,0x6600},{0x6c00,0x4620,0x06c0,0x8c40},{0x4e00,0x4640,0x0e40,0x4c40},{0xc600,0x2640,0x0c60,0x4c80}}
local colors={0x24d1e0,0x4a72ff,0xff9d2e,0xffd426,0x3ddc6b,0xb45bff,0xff4d5e}
local gravity={800,720,630,550,470,380,300,220,130,100,80,80,80,70,70,70,50,50,50,30}
local s
local function bit(piece,rot,row,col) return (shapes[piece][rot+1] & (0x8000 >> (row*4+col)))~=0 end
local function collide(piece,rot,x,y)
 for row=0,3 do for col=0,3 do if bit(piece,rot,row,col) then
  local xx,yy=x+col,y+row
  if xx<0 or xx>=10 or yy>=20 or (yy>=0 and s.board[yy*10+xx+1]~=0) then return true end
 end end end
 return false
end
local function take()
 if #s.bag==0 then
  s.bag={1,2,3,4,5,6,7}
  for i=7,2,-1 do local j=math.random(i); s.bag[i],s.bag[j]=s.bag[j],s.bag[i] end
 end
 return table.remove(s.bag)
end
local function banner(text) s.banner=text; s.banner_ms=1200 end
local function spawn(piece)
 s.piece,s.rot,s.x,s.y=piece,0,3,0
 s.resting=false; s.resets=0; s.lock=500; s.gravity=gravity[s.level+1]
 if collide(piece,0,3,0) then
  s.state=2; s.best=math.max(s.best,s.score); game.save_score(s.best); game.tone(0,0); game.haptic(1)
 end
end
local function next_piece()
 local piece=s.next; s.next=take(); s.hold_used=false; spawn(piece)
end
local function distance()
 local d=0; while not collide(s.piece,s.rot,s.x,s.y+d+1) do d=d+1 end; return d
end
local function lock_piece()
 for row=0,3 do for col=0,3 do if bit(s.piece,s.rot,row,col) then
  local x,y=s.x+col,s.y+row
  if y>=0 and y<20 and x>=0 and x<10 then s.board[y*10+x+1]=s.piece end
 end end end
 s.rows={}; local count=0
 for y=0,19 do
  local full=true; for x=0,9 do if s.board[y*10+x+1]==0 then full=false end end
  if full then s.rows[y]=true; count=count+1 end
 end
 if count>0 then
  local scores={100,300,500,800}; s.score=s.score+scores[count]*(s.level+1); s.lines=s.lines+count
  local previous=s.level; s.level=math.min(19,math.floor(s.lines/10)); s.flash=130
  if s.level>previous then banner("LEVEL UP") end
  if count==4 then banner("TETRIS") end
  game.haptic(1)
 else next_piece() end
end
local function collapse()
 local dest=19
 for y=19,0,-1 do if not s.rows[y] then
  for x=0,9 do s.board[dest*10+x+1]=s.board[y*10+x+1] end; dest=dest-1
 end end
 for i=1,(dest+1)*10 do s.board[i]=0 end
 s.rows={}; next_piece()
end
local function reset_lock()
 if s.resting and s.resets<15 then s.resets=s.resets+1; s.lock=500 end
end
local function move(dx,dy)
 if collide(s.piece,s.rot,s.x+dx,s.y+dy) then return false end
 s.x=s.x+dx; s.y=s.y+dy; reset_lock(); return true
end
local function rotate()
 local rot=(s.rot+1)%4
 for _,kick in ipairs({{0,0},{-1,0},{1,0},{-2,0},{2,0},{0,-1}}) do
  if not collide(s.piece,rot,s.x+kick[1],s.y+kick[2]) then
   s.rot=rot;s.x=s.x+kick[1];s.y=s.y+kick[2];reset_lock();return
  end
 end
end
local function hold()
 if s.hold_used then return end
 s.hold_used=true;local old=s.hold;s.hold=s.piece
 if old==0 then spawn(s.next);s.next=take() else spawn(old) end
end
local function hard_drop()
 local d=distance();s.y=s.y+d;s.score=s.score+d*2;game.haptic(0);lock_piece()
end
local function reset(playing)
 s={state=playing and 1 or 0,best=s and s.best or 0,board={},bag={},rows={},score=0,lines=0,level=0,
    hold=0,hold_used=false,flash=0,banner="",banner_ms=0,held=0,das=0,music=0,note=1}
 for i=1,200 do s.board[i]=0 end
 s.next=take();next_piece()
end
function init(best) game.repeat_input(170,55);s={best=best};reset(false) end
function event(kind,x,y)
 if kind==6 then
  local down=s.touch;s.touch=nil;s.held=0
  if not down or s.state~=1 or s.flash>0 then return end
  if down.x>=33 and down.x<=67 then
   if y-down.y>8 then hard_drop() elseif down.y-y>8 then hold() else rotate() end
  end
  return
 end
 if s.state~=1 then reset(true);return end
 if s.flash>0 then return end
 if kind==0 or kind==1 then rotate()
 elseif kind==2 then if move(0,1) then s.score=s.score+1;s.gravity=gravity[s.level+1] end
 elseif kind==3 then move(-1,0)
 elseif kind==4 then move(1,0)
 elseif kind==5 then
  s.touch={x=x,y=y};s.held=x<33 and -1 or (x>67 and 1 or 0);s.das=170
  if s.held~=0 then move(s.held,0) end
 end
end
-- Korobeiniki, public-domain folk melody. Frequencies/durations are package data.
local a={{659,400},{494,200},{523,200},{587,400},{523,200},{494,200},{440,400},{440,200},{523,200},
 {659,400},{587,200},{523,200},{494,600},{523,200},{587,400},{659,400},{523,400},{440,400},{440,400},{0,400},
 {587,600},{698,200},{880,400},{784,200},{698,200},{659,600},{523,200},{659,400},{587,200},{523,200},
 {494,400},{494,200},{523,200},{587,400},{659,400},{523,400},{440,400},{440,400},{0,400}}
local b={{659,800},{523,800},{587,800},{494,800},{523,800},{440,800},{415,800},{494,800},
 {659,800},{523,800},{587,800},{494,800},{523,400},{659,400},{880,800},{831,800},{0,400}}
local music={};for _,part in ipairs({a,a,b,b}) do for _,note in ipairs(part) do music[#music+1]=note end end
function update(dt)
 if s.state~=1 then return end
 local ms=dt*1000
 s.music=s.music-ms
 if s.music<=0 then local n=music[s.note];game.tone(n[1],n[2]);s.music=n[2];s.note=s.note%#music+1 end
 s.banner_ms=math.max(0,s.banner_ms-ms);if s.banner_ms==0 then s.banner="" end
 if s.flash>0 then s.flash=math.max(0,s.flash-ms);if s.flash==0 then collapse() end;return end
 if s.held~=0 then s.das=s.das-ms;if s.das<=0 then move(s.held,0);s.das=55 end end
 s.gravity=s.gravity-ms
 if s.gravity<=0 then
  s.gravity=math.max(s.gravity+gravity[s.level+1],1)
  if not collide(s.piece,s.rot,s.x,s.y+1) then s.y=s.y+1;s.resting=false end
 end
 if collide(s.piece,s.rot,s.x,s.y+1) then
  if not s.resting then s.resting=true;s.lock=500 end
  s.lock=s.lock-ms;if s.lock<=0 then lock_piece() end
 else s.resting=false end
end
local function cell(x,y,value,ghost)
 if value==0 or y<0 then return end
 local color=colors[value] or 0xffffff
 if ghost then color=0x244556 end
 r(33+x*3.4,14+y*3.4,3.1,3.1,color,.3)
end
local function preview(piece,x,y)
 if piece==0 then return end
 for row=0,3 do for col=0,3 do if bit(piece,0,row,col) then r(x+col*2.5,y+row*2.5,2.2,2.2,colors[piece],.3) end end end
end
function draw()
 r(0,0,100,100,0x0c1726);r(32,13,36,70,0x213348,1);r(33,14,34,68,0x101d2b)
 for y=0,19 do for x=0,9 do cell(x,y,s.flash>0 and s.rows[y] and 8 or s.board[y*10+x+1]) end end
 if s.state==1 and s.flash==0 then
  local ghost=s.y+distance()
  for row=0,3 do for col=0,3 do if bit(s.piece,s.rot,row,col) then cell(s.x+col,ghost+row,s.piece,true) end end end
  for row=0,3 do for col=0,3 do if bit(s.piece,s.rot,row,col) then cell(s.x+col,s.y+row,s.piece) end end end
 end
 t(20,4,60,"TETRIS",0x7bdfff,14)
 t(5,25,26,"SCORE",0x9baebd,10);t(5,32,26,tostring(s.score),0xffffff,12)
 t(5,44,26,"LEVEL "..(s.level+1),0xffffff,10);t(5,53,26,"LINES "..s.lines,0x9baebd,10)
 t(69,25,26,"NEXT",0x9baebd,10);preview(s.next,77,35)
 t(69,49,26,"HOLD",0x9baebd,10);preview(s.hold,77,59)
 if s.banner~="" then t(20,74,60,s.banner,0xffd426,12) end
 if s.state~=1 then
  r(12,23,76,51,0x152c40,4);t(14,27,72,s.state==0 and "TETRIS" or "GAME OVER",0x7bdfff,14)
  t(14,39,72,"BEST "..s.best,0xffffff,12)
  t(14,49,72,"Tap sides to move, center to turn",0xbdcddb,10)
  t(14,57,72,"Swipe down: drop / up: hold",0xbdcddb,10)
  t(14,65,72,"TAP OR PRESS TO START",0xffffff,10)
 end
end
function inspect() return s end
