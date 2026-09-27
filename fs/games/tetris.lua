-- pubmote-game {"api":1,"id":"tetris","title":"Tetris","version":"1.0.0"}
local r,t=game.rect,game.text
local shapes={{0x0f00,0x2222,0x00f0,0x4444},{0x8e00,0x6440,0x0e20,0x44c0},{0x2e00,0x4460,0x0e80,0xc440},
 {0x6600,0x6600,0x6600,0x6600},{0x6c00,0x4620,0x06c0,0x8c40},{0x4e00,0x4640,0x0e40,0x4c40},{0xc600,0x2640,0x0c60,0x4c80}}
local colors={0x24d1e0,0x4a72ff,0xff9d2e,0xffd426,0x3ddc6b,0xb45bff,0xff4d5e}
local gravity={800,720,630,550,470,380,300,220,130,100,80,80,80,70,70,70,50,50,50,30}
local s
local music
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
  for i=7,2,-1 do local j=1+game.random(i); s.bag[i],s.bag[j]=s.bag[j],s.bag[i] end
 end
 return table.remove(s.bag,1)
end
local function banner(text) s.banner=text; s.banner_ms=1200 end
local function spawn(piece)
 s.piece,s.rot,s.x,s.y=piece,0,3,0
 s.resting=false; s.resets=0; s.lock=500; s.gravity=gravity[s.level+1]
 if collide(piece,0,3,0) then
  s.state=2;if s.score>s.best then s.best=s.score;game.save_score(s.best);banner("NEW BEST") else banner("") end;game.tone(0,0);game.haptic(1)
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
  if s.level>previous then banner("LEVEL UP");game.haptic(2) end
  if count==4 then banner("TETRIS");game.haptic(5) else game.haptic(3) end
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
 local d=distance();s.y=s.y+d;s.score=s.score+d*2;if d>0 then game.haptic(0) end;lock_piece()
end
local function reset(playing)
 s={state=playing and 1 or 0,best=s and s.best or 0,board={},bag={},rows={},score=0,lines=0,level=0,
    hold=0,hold_used=false,flash=0,banner="",banner_ms=0,held=0,das=0,music=0,note=1}
 for i=1,200 do s.board[i]=0 end
 if playing then s.next=take();next_piece();game.sequence(music,true) else s.next=0;s.piece=1;s.rot=0;s.x=3;s.y=0 end
end
function init(best) game.repeat_input(170,55);s={best=best};reset(false) end
local function layout()
 local w,h,square,stick,button=game.screen();local scale=math.min(w,h)/240
 local function rnd(v) return math.floor(v+.5) end
 local footer=rnd(44*scale);local fy=h-footer
 local uw=square and w or rnd(w*.707);local uy=square and 0 or rnd((h-rnd(h*.707))/2)
 local uh=fy-uy;local cell=math.max(3,math.min(math.floor(uh/20),math.floor((uw-rnd(40*scale)*2)/10)))
 local wx=rnd((w-cell*10)/2);local wy=uy+rnd((uh-cell*20)/2);local pad=rnd(4*scale)
 local inset=square and pad or rnd(w*.1);local lw=wx-inset-pad;local pc=math.max(3,rnd(cell*.5))
 return {w=w,h=h,scale=scale,footer=footer,fy=fy,uy=uy,uh=uh,cell=cell,wx=wx,wy=wy,inset=inset,lw=lw,pad=pad,pc=pc,slim=lw<rnd(56*scale),square=square,stick=stick,button=button}
end
function event(kind,x,y)
 local l=layout();local xx,yy=x*l.w/100,y*l.h/100
 if kind==5 then
  s.touch={y=yy,zone=xx<l.wx and -1 or xx>l.wx+l.cell*10 and 1 or 0,active=yy<l.fy,swiped=false}
  if s.touch.active and s.touch.zone~=0 then
   if s.state~=1 then reset(true);return end
   s.held=s.touch.zone;s.das=170;move(s.held,0)
  end
  return
 elseif kind==7 then
  local down=s.touch
  if down and down.active and not down.swiped and down.zone==0 and math.abs(yy-down.y)>math.floor(22*l.scale+.5) then
   down.swiped=true;s.held=0
   if s.state~=1 then reset(true);return end
   if yy>down.y then hard_drop() else hold() end
   return
  end
  return false
 elseif kind==6 then
  local down=s.touch;s.touch=nil
  if down and down.active then
   if down.zone~=0 then s.held=0 elseif not down.swiped then
    if s.state~=1 then reset(true) else rotate() end
   end
  end
  return
 end
 if s.state~=1 then reset(true);return end
 if kind==0 or kind==1 then rotate()
 elseif kind==2 then if move(0,1) then s.score=s.score+1;s.gravity=gravity[s.level+1] end
 elseif kind==3 then move(-1,0)
 elseif kind==4 then move(1,0)
 elseif kind==8 then s.held=0;hard_drop()
 elseif kind==9 then s.held=0;hold() end
end
-- Korobeiniki, public-domain folk melody. Frequencies/durations are package data.
local a={{659,400},{493,200},{523,200},{587,400},{523,200},{493,200},{440,400},{440,200},{523,200},
 {659,400},{587,200},{523,200},{493,600},{523,200},{587,400},{659,400},{523,400},{440,400},{440,400},{0,400},
 {587,600},{698,200},{880,400},{784,200},{698,200},{659,600},{523,200},{659,400},{587,200},{523,200},
 {493,400},{493,200},{523,200},{587,400},{659,400},{523,400},{440,400},{440,400},{0,400}}
local b={{659,800},{523,800},{587,800},{493,800},{523,800},{440,800},{415,800},{493,800},
 {659,800},{523,800},{587,800},{493,800},{523,400},{659,400},{880,800},{831,800},{0,400}}
music={};for _,part in ipairs({a,a,b,b}) do for _,note in ipairs(part) do music[#music+1]=note end end
function update(dt)
 if s.state~=1 then return false end
 local ms=math.floor(dt*1000+.5)
 s.banner_ms=math.max(0,s.banner_ms-ms);if s.banner_ms==0 then s.banner="" end
 if s.flash>0 then s.flash=math.max(0,s.flash-ms);if s.flash==0 then collapse() end;return end
 if s.held~=0 then s.das=s.das-ms;if s.das<=0 then move(s.held,0);s.das=55 end end
 s.gravity=s.gravity-ms
 if s.gravity<=0 then
  s.gravity=s.gravity+gravity[s.level+1];if s.gravity<=0 then s.gravity=gravity[s.level+1] end
  if not collide(s.piece,s.rot,s.x,s.y+1) then s.y=s.y+1;s.resting=false end
 end
 if collide(s.piece,s.rot,s.x,s.y+1) then
  if not s.resting then s.resting=true;s.lock=500 end
  s.lock=s.lock-ms;if s.lock<=0 then lock_piece() end
 else s.resting=false end
end
function draw()
 local l=layout();local w,h=l.w,l.h;local cell=l.cell
 local function box(x,y,ww,hh,c,alpha) r(x/w*100,y/h*100,ww/w*100,hh/h*100,c,0,0,0,alpha or 255) end
 local function rnd(v) return math.floor(v+.5) end
 local function spacer(px) return {"",10,0,false,false,rnd(px*l.scale)/h*100} end
 r(0,0,100,100,0)
 r((l.wx-1)/w*100,(l.wy-1)/h*100,(cell*10+2)/w*100,(cell*20+2)/h*100,0x0b0b10,0,100/w,0x414141)
 local view={};for i=1,200 do view[i]=s.flash>0 and s.rows[math.floor((i-1)/10)] and 9 or s.board[i] end
 if s.state==1 and s.flash==0 then
  local ghost=s.y+distance()
  for row=0,3 do for col=0,3 do if bit(s.piece,s.rot,row,col) then
   local x,y=s.x+col,ghost+row;local i=y*10+x+1
   if y>=0 and y<20 and x>=0 and x<10 and view[i]==0 then view[i]=s.piece+10 end
  end end end
  for row=0,3 do for col=0,3 do if bit(s.piece,s.rot,row,col) then
   local x,y=s.x+col,s.y+row;if y>=0 and y<20 and x>=0 and x<10 then view[y*10+x+1]=s.piece end
  end end end
 end
 for i,v in ipairs(view) do if v~=0 then box(l.wx+((i-1)%10)*cell,l.wy+math.floor((i-1)/10)*cell,cell-1,cell-1,v==9 and 0xffffff or colors[v>10 and v-10 or v],v>10 and 51 or 255) end end
 local stat=l.slim and 11 or 14;local sub=l.slim and 10 or 12
 game.column(l.inset/w*100,l.wy/h*100,l.lw/w*100,cell*20/h*100,rnd(l.scale)/h*100,0,
  {{"SCORE",10,0x9a9a9a},{tostring(s.score),stat,0xffffff,true},spacer(8),{"BEST",10,0x9a9a9a},{tostring(s.best),sub,0x9a9a9a,true},spacer(10),{"<",28,0x414141}})
 local preview={};for row=0,3 do for col=0,3 do preview[#preview+1]=s.next>0 and bit(s.next,0,row,col) and colors[s.next] or 0 end end
 game.column((l.wx+cell*10+l.pad)/w*100,l.wy/h*100,l.lw/w*100,cell*20/h*100,rnd(l.scale)/h*100,0,
  {{"LEVEL",10,0x9a9a9a},{tostring(s.level+1),stat,0xffffff,true},spacer(6),{"LINES",10,0x9a9a9a},{tostring(s.lines),sub,0x9a9a9a,true},spacer(8),{"NEXT",10,0x9a9a9a},{"",10,0,false,false,l.pc*4/h*100,preview,l.pc/w*100},spacer(8),{">",28,0x414141}})
 if s.banner~="" and s.state==1 then t(0,(l.wy+cell*20*.32)/h*100,100,s.banner,0x2196f3,14,0,false,true) end
 if s.state~=1 then
  r(0,l.uy/h*100,100,l.uh/h*100,0,0,0,0,230)
  local rows
  if s.state==0 then
   rows={{"TETRIS",28,0x2196f3,false,true},spacer(6),{"Tap the sides to move",10,0x9a9a9a},{"Tap the well to rotate",10,0x9a9a9a},{"Swipe down to drop",10,0x9a9a9a},{"Swipe up to hold",10,0x9a9a9a}}
   if l.stick then rows[#rows+1]={"Stick moves and drops",10,0x2196f3} end
   if l.button then rows[#rows+1]={"Button rotates",10,0x2196f3} end
   rows[#rows+1]=spacer(10);rows[#rows+1]={"TAP TO START",12,0xffffff,false,true}
  else
   rows={{"GAME OVER",14,0xff6369,false,true},spacer(8),{"SCORE",10,0x9a9a9a},{tostring(s.score),28,0xffffff,true},{"BEST "..s.best,11,0x9a9a9a}}
   if s.banner~="" then rows[#rows+1]={s.banner,12,0x2196f3,false,true} end
   rows[#rows+1]=spacer(10);rows[#rows+1]={"TAP TO RETRY",12,0xffffff,false,true}
  end
  local pad=(l.square and 24 or 36)*l.scale
  game.column(pad/w*100,l.uy/h*100,(w-pad*2)/w*100,l.uh/h*100,rnd((s.state==0 and 3 or 2)*l.scale)/h*100,0,rows)
 end
 local ew=rnd(76*l.scale);local eh=30*l.scale;local ey=l.fy+math.max(0,(l.footer-eh)/2-rnd(2*l.scale))
 game.exit((w-ew)/2/w*100,ey/h*100,ew/w*100,eh/h*100)
end
function inspect() return s end
