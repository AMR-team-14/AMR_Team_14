# edge_front: Edge windows were minimized, so CDP screenshots of the Claude tab hang (no frames are produced).
# 'show' restores the minimized Edge windows without activating them and keeps them topmost; 'hide' undoes both (minimize again).
# Only window state is changed (no settings). Usage: edge_front.ps1 show|hide|list
param([string]$mode = 'list')
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class WinF {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public static List<IntPtr> Edge() {
    var o = new List<IntPtr>();
    EnumWindows((h, l) => { var c = new StringBuilder(200); GetClassName(h, c, 200); if (c.ToString() == "Chrome_WidgetWin_1" && IsWindowVisible(h)) { uint pid; GetWindowThreadProcessId(h, out pid); string pn = ""; try { pn = System.Diagnostics.Process.GetProcessById((int)pid).ProcessName; } catch {} var sb = new StringBuilder(400); GetWindowText(h, sb, 400); if (pn == "msedge" && sb.Length > 0) o.Add(h); } return true; }, IntPtr.Zero);
    return o;
  }
  public static string Info(IntPtr h) { var sb = new StringBuilder(400); GetWindowText(h, sb, 400); RECT r; GetWindowRect(h, out r); var t = sb.ToString(); return (IsIconic(h) ? "min " : "open") + " [" + r.L + "," + r.T + " " + (r.R - r.L) + "x" + (r.B - r.T) + "] " + (t.Length > 50 ? t.Substring(0, 50) : t); }
}
"@
foreach ($h in [WinF]::Edge()) {
  if ($mode -eq 'show') { if ([WinF]::IsIconic($h)) { [void][WinF]::ShowWindow($h, 4) }; [void][WinF]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x13) }
  elseif ($mode -eq 'untop') { [void][WinF]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x13) }
  elseif ($mode -eq 'hide') { [void][WinF]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x13); [void][WinF]::ShowWindow($h, 7) }
  [WinF]::Info($h)
}
