// CoDGLFix.exe - lets Call of Duty (2003) and Call of Duty: United Offensive load missions on modern
// graphics cards.
//
// Set as the game's Steam launch option:
//     "<Call of Duty folder>\CoDGLFix\CoDGLFix.exe" %command%
// Steam then runs this program with the game's own start command (the game's exe first, then its
// arguments, plus anything written after %command%). For the single-player games (CoDSP.exe,
// CoDUOSP.exe) it starts the game exactly as Steam asked and loads codglfix.dll (next to this exe)
// into it. Anything else (Multiplayer) is started unchanged. It changes no files.
//
// Build (32-bit, so LoadLibraryA matches the 32-bit game):
//   csc -target:winexe -platform:x86 -optimize -out:CoDGLFix.exe -r:System.Windows.Forms.dll CoDGLFix.cs

using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

static class CoDGLFix
{
    static readonly string[] SinglePlayerExes = { "CoDSP", "CoDUOSP" };

    [STAThread]
    static int Main(string[] args)
    {
        string here = AppDomain.CurrentDomain.BaseDirectory;
        if (args.Length == 0 || !args[0].EndsWith(".exe", StringComparison.OrdinalIgnoreCase) || !File.Exists(args[0]))
        {
            MessageBox.Show("CoDGLFix is started by Steam, not by double-clicking it.\n\n"
                + "In Steam, right-click Call of Duty (or Call of Duty: United Offensive) > Properties > General > "
                + "Launch Options, and enter:\n\n\"" + Path.Combine(here, "CoDGLFix.exe") + "\" %command%\n\nThen press Play.",
                "CoDGLFix", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return 1;
        }

        string exe = Path.GetFullPath(args[0]);
        var rest = new StringBuilder();
        for (int i = 1; i < args.Length; i++)
        {
            if (rest.Length > 0) rest.Append(' ');
            rest.Append(args[i].IndexOf(' ') >= 0 ? "\"" + args[i] + "\"" : args[i]);
        }

        var log = new StringBuilder();
        log.AppendLine(DateTime.Now.ToString("s") + " " + exe + " " + rest);
        Process game;
        try
        {
            game = Process.Start(new ProcessStartInfo(exe, rest.ToString()) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(exe) });
        }
        catch (Exception e)
        {
            MessageBox.Show("Could not start " + exe + ":\n" + e.Message, "CoDGLFix", MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }

        bool singlePlayer = SinglePlayerExes.Any(n => Path.GetFileNameWithoutExtension(exe).Equals(n, StringComparison.OrdinalIgnoreCase));
        if (singlePlayer)
        {
            // The fix has to be in the game before its renderer starts, so load it right away
            // (retrying for a few seconds while the new process finishes starting).
            string dll = Path.Combine(here, "codglfix.dll");
            string why = "codglfix.dll is missing next to CoDGLFix.exe";
            bool ok = false;
            if (File.Exists(dll))
                for (int i = 0; i < 50 && !ok && !game.HasExited; i++)
                {
                    ok = InjectDll(game, dll, out why);
                    if (!ok) Thread.Sleep(100);
                }
            log.AppendLine(ok ? "fix loaded" : "fix NOT loaded: " + why);
        }
        else
            log.AppendLine("not a single-player exe: started unchanged");
        try { File.WriteAllText(Path.Combine(here, "CoDGLFix.log"), log.ToString()); } catch { }

        game.WaitForExit();   // Steam counts the game as running while this program runs
        return 0;
    }

    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, uint size, uint type, uint prot);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, uint n, out IntPtr w);
    [DllImport("kernel32.dll")] static extern IntPtr CreateRemoteThread(IntPtr p, IntPtr sa, uint ss, IntPtr start, IntPtr arg, uint flags, IntPtr id);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr h, uint ms);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeThread(IntPtr h, out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetModuleHandleA(string name);
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr m, string name);

    // Load a DLL into the (32-bit) game with LoadLibraryA on a thread in the game.
    static bool InjectDll(Process game, string dll, out string why)
    {
        why = "";
        IntPtr h = OpenProcess(0x1F0FFF, false, game.Id);
        if (h == IntPtr.Zero) { why = "OpenProcess failed"; return false; }
        try
        {
            byte[] path = Encoding.ASCII.GetBytes(dll + "\0");
            IntPtr mem = VirtualAllocEx(h, IntPtr.Zero, (uint)path.Length, 0x3000, 0x04);
            IntPtr w;
            if (mem == IntPtr.Zero || !WriteProcessMemory(h, mem, path, (uint)path.Length, out w)) { why = "write failed"; return false; }
            IntPtr ll = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
            IntPtr t = CreateRemoteThread(h, IntPtr.Zero, 0, ll, mem, 0, IntPtr.Zero);
            if (t == IntPtr.Zero) { why = "CreateRemoteThread failed"; return false; }
            WaitForSingleObject(t, 10000);
            uint code; GetExitCodeThread(t, out code);
            CloseHandle(t);
            if (code == 0) { why = "LoadLibrary failed in the game"; return false; }
            return true;
        }
        finally { CloseHandle(h); }
    }
}
