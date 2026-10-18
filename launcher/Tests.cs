using System;
using System.IO;
using System.Drawing;
using System.Windows.Forms;
internal class Tests {
    static void Check(bool ok,string name) {if(!ok)throw new Exception(name);Console.WriteLine("PASS "+name);}
    [STAThread] static int Main(string[] args) {
        Check(Data.Quote(@"C:\a b\test.pk3")=="\"C:\\a b\\test.pk3\"","paths with spaces");
        Check(Data.Quote("a\"b")=="\"a\\\"b\"","embedded quote");
        Check(Data.Quote("end\\")=="\"end\\\\\"","trailing backslash");
        Check(Data.Iwad(args[0]),"real Doom II IWAD validation");
        Check(!Data.Iwad(args[1]),"PK3 is not an IWAD");
        Check(Data.Pk3Issue(args[1])!=null,"actual ZScript mod receives compatibility explanation");
        Check(Data.FluidPack(args[2])==1&&Data.FluidPack(args[3])==2,"exact Fluid Weapons and SFX packs recognized");
        Check(Data.Pk3Issue(args[2])==null&&Data.Pk3Issue(args[3])==null,"native Fluid packs accepted");
        string launch=Data.Arguments(args[0],new[]{args[2],args[3]});
        Check(launch.Contains("-fluidweapons "+Data.Quote(args[2]))&&launch.Contains("-modsounds "+Data.Quote(args[2])+" "+Data.Quote(args[3]))&&!launch.Contains("-file"),"both packs routed to native artwork and sound loaders");
        bool rejected=false;try{Data.Arguments(args[0],new[]{args[3],args[2]});}catch(IOException){rejected=true;}
        Check(rejected,"sound add-on before main pack rejected");
        rejected=false;try{Data.Arguments(args[0],new[]{args[2],args[1]});}catch(IOException){rejected=true;}
        Check(rejected,"unrelated ZScript not accidentally enabled");
        if(args.Length>5) {
            rejected=false;try{Data.Arguments(args[0],new[]{args[4]});}catch(IOException){rejected=true;}
            Check(rejected,"SIGIL rejects Doom II IWAD");
            Check(Data.Arguments(args[5],new[]{args[4],args[2],args[3]}).Contains(Data.Quote(args[4])),"SIGIL with Ultimate Doom and Fluid packs accepted");
        }
        var found=Data.Scan(new[]{Path.GetDirectoryName(args[0])});Check(found.Contains(args[0]),"IWAD discovery");
        if(args.Length>6) {
            Check(Data.FluidPack(args[6])==3&&Data.Pk3Issue(args[6])==null,"exact FluidMonsters pack recognized");
            Check(Data.Arguments(args[0],new[]{args[6]}).Contains("-fluidmonsters"),"FluidMonsters works independently");
            Check(Data.Arguments(args[0],new[]{args[6],args[2],args[3]}).Contains("-fluidmonsters"),"FluidMonsters works with weapons and sounds");
            rejected=false;try{Data.Arguments(args[0],new[]{args[6],args[6]});}catch(IOException){rejected=true;}
            Check(rejected,"duplicate FluidMonsters rejected");
        }
        string engine=Data.Engine();Check(File.Exists(engine),"embedded engine extraction");Check(Data.Engine()==engine,"embedded engine integrity recheck");
        File.WriteAllText("embedded-engine-path.txt",engine);
        Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
        using(var form=new Launcher()) { form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-20000,-20000);form.Show();Application.DoEvents();using(var b=new Bitmap(form.Width,form.Height)) {form.DrawToBitmap(b,new Rectangle(0,0,b.Width,b.Height));b.Save("launcher-preview.png");} }
        Console.WriteLine("All launcher checks passed");return 0;
    }
}
