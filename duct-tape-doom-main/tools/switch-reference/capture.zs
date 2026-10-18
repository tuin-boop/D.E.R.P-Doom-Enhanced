version "4.14.0"
class SwitchCapture : EventHandler {
 override void WorldTick(){
  let p=players[0].mo;if(!p)return;
  String names[]={"Fist","Pistol","Shotgun","Chaingun","RocketLauncher","PlasmaRifle","BFG9000","Chainsaw","SuperShotgun"};
  int elapsed=level.time-70;if(elapsed<0||elapsed>=5760)return;
  int sample=elapsed/80;int wi=sample%9;int t=elapsed%80;Name cls=Name("MRIntWeaps_"..names[wi]);let w=Weapon(p.FindInventory(cls));
  if(t==0){
   p.GiveInventoryType(cls);w=Weapon(p.FindInventory(cls));
   players[0].ReadyWeapon=w;players[0].PendingWeapon=WP_NOCHANGE;
   for(int id=-1100;id<=1700;id++)if(id!=1&&players[0].FindPSprite(id))players[0].SetPSprite(id,null);
   players[0].SetPSprite(1,w.FindState("Select"));
  }
  if(t==40){players[0].PendingWeapon=Weapon(p.FindInventory('MRIntWeaps_Pistol'));players[0].SetPSprite(1,w.FindState("Deselect"));}
  let base=players[0].FindPSprite(1);
  Console.Printf("SF %d %d %d %f %f",sample,wi,t,base?base.X:0,base?base.Y:0);
  for(int id=-1100;id<=1700;id++){
   let s=players[0].FindPSprite(id);if(!s||!s.CurState)continue;
   let tex=s.CurState.GetSpriteTexture(0,0,(0,0),s.Sprite,s.Frame);
   String name=TexMan.GetName(tex);if(name=="TNT1A0"||name=="")continue;
   Console.Printf("SL %d %s %f %f %f %f %f %f %f %f %d %d %d %d",id,name,s.X,s.Y,s.Scale.X,s.Scale.Y,s.Rotation,s.Pivot.X,s.Pivot.Y,s.Alpha,s.bAddWeapon,s.bFlip,s.bMirror,s.bInterpolate);
  }
 }
}


