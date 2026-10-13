// ------------------------------------------------------------------------------
//                           S Y N - S H E L L
//
//   PtySession, Windows: PtySession.cpp's holder and attach on Windows'
//   own parts. The shell runs in a pseudoconsole (ConPTY) that
//   `syn-shell --pty-hold` owns and serves on a named pipe only this user
//   can open. QLocalSocket is a named pipe on Windows, so TermView talks
//   to it unchanged; syn-shell-attach, inside Windows Terminal, is the
//   other client. One client at a time, the newest taking over: that's the
//   move to the terminal, nothing restarted.
//
//   The shell is PowerShell (pwsh, else Windows PowerShell; cmd if neither
//   is there, with no integration), given kPowerShell after the user's
//   profile has run. Its prompt reports the folder as OSC 7, as zsh does
//   on Linux. The other way, Windows has no SIGUSR1 and no way to ask
//   whether the shell itself is in the foreground: the folder asked for
//   waits in a file, PowerShell takes it when it's idle at an empty
//   prompt, and only while it's fresh, so a running program or a
//   half-typed line lets it go stale and it's dropped.
//
//   SYN-OS     : The Syntax Operating System
//   Component  : SYN-SHELL (Desktop)
//   Author     : William Hayward-Holland (Syntax990)
//   License    : MIT License
// ------------------------------------------------------------------------------

#include "PtySession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QWinEventNotifier>

#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace PtySession {

namespace {

// Run by PowerShell after the user's profile, in the global scope. The
// folder goes out percent-encoded throughout, so it's plain ASCII
// whatever the console's code page; TermView decodes it.
const char kPowerShell[] = R"PS1(# SYN-SHELL's PowerShell integration.
$global:SynShellPrompt = $function:prompt
function global:prompt {
  $here = $ExecutionContext.SessionState.Path.CurrentLocation
  if ($here.Provider.Name -eq 'FileSystem') {
    $p = $here.ProviderPath -replace '\\', '/'
    [Console]::Write([char]27 + ']7;file://' + $env:COMPUTERNAME + '/' + [uri]::EscapeDataString($p) + [char]7)
  }
  & $global:SynShellPrompt
}
$null = Register-EngineEvent -SourceIdentifier PowerShell.OnIdle -SupportEvent -Action {
  $f = $env:SYN_SHELL_CD_FILE
  if (-not $f -or -not [IO.File]::Exists($f)) { return }
  $age = ([DateTime]::UtcNow - [IO.File]::GetLastWriteTimeUtc($f)).TotalSeconds
  $d = [IO.File]::ReadAllText($f, [Text.Encoding]::UTF8)
  [IO.File]::Delete($f)
  if ($age -gt 2 -or -not [IO.Directory]::Exists($d)) { return }
  $now = $ExecutionContext.SessionState.Path.CurrentFileSystemLocation.ProviderPath
  if ([IO.Path]::GetFullPath($d).TrimEnd('\') -eq $now.TrimEnd('\')) { return }
  Set-Location -LiteralPath $d
  [Microsoft.PowerShell.PSConsoleReadLine]::InvokePrompt()
}
)PS1";

QString runtimeDir()
{
  const QString dir = QDir::tempPath() + QStringLiteral("/syn-shell");
  QDir().mkpath(dir);
  return dir;
}

std::wstring wide(const QString &s)
{
  return s.toStdWString();
}

// Accumulates client bytes and hands back whole packets.
struct PacketReader
{
  QByteArray buf;
  bool next(char *type, QByteArray *payload)
  {
    if (buf.size() < 5)
      return false;
    quint32 len;
    memcpy(&len, buf.constData() + 1, 4);
    if (buf.size() < 5 + qsizetype(len))
      return false;
    *type = buf.at(0);
    *payload = buf.mid(5, len);
    buf.remove(0, 5 + qsizetype(len));
    return true;
  }
};

// The shell, or `command`, on the pseudoconsole: its process handle, or
// null. PowerShell gets kPowerShell as an encoded command, so it runs
// whatever the execution policy and the profile still loads first.
HANDLE startShell(HPCON pc, const QString &cwd, const QStringList &command, bool *powershell)
{
  QStringList args;
  *powershell = false;
  if (!command.isEmpty()) {
    args = command;
    const QString exe = QStandardPaths::findExecutable(args.first());
    if (!exe.isEmpty())
      args.first() = QDir::toNativeSeparators(exe);
    // A .cmd or .bat (code, a script) runs under cmd, not on its own.
    const QString suffix = QFileInfo(args.first()).suffix().toLower();
    if (suffix == QLatin1String("cmd") || suffix == QLatin1String("bat"))
      args = QStringList{qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe")), QStringLiteral("/c")} + args;
  } else {
    const QString shell = shellProgram();
    args << QDir::toNativeSeparators(shell);
    if (isPowerShell(shell)) {
      *powershell = true;
      const QString script = QString::fromUtf8(kPowerShell);
      const QByteArray utf16(reinterpret_cast<const char *>(script.utf16()), script.size() * 2);
      args << QStringLiteral("-NoLogo") << QStringLiteral("-NoExit")
           << QStringLiteral("-EncodedCommand") << QString::fromLatin1(utf16.toBase64());
    }
  }

  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
  std::vector<char> attrBuf(size);
  auto *attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
  if (!InitializeProcThreadAttributeList(attrs, 1, 0, &size))
    return nullptr;
  if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pc, sizeof(pc),
                                 nullptr, nullptr)) {
    DeleteProcThreadAttributeList(attrs);
    return nullptr;
  }
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  // Null standard handles: the shell's are the pseudoconsole's, never
  // something this process was handed.
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  si.lpAttributeList = attrs;

  QString dir = QDir::toNativeSeparators(cwd);
  if (!QFileInfo(cwd).isDir())
    dir = QDir::toNativeSeparators(QDir::homePath());
  std::wstring line = wide(commandLine(args));
  const std::wstring wdir = wide(dir);
  PROCESS_INFORMATION pi{};
  const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE,
                                 EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                                 nullptr, wdir.c_str(), &si.StartupInfo, &pi);
  DeleteProcThreadAttributeList(attrs);
  if (!ok)
    return nullptr;
  CloseHandle(pi.hThread);
  return pi.hProcess;
}

// Overlapped I/O on the attach side's pipe: two threads use it at once,
// which a handle opened for plain blocking I/O would serialise.
bool pipeWrite(HANDLE pipe, const char *data, DWORD len)
{
  OVERLAPPED ov{};
  ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  bool ok = true;
  while (ok && len > 0) {
    DWORD n = 0;
    if (!WriteFile(pipe, data, len, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
      ok = false;
    else if (!GetOverlappedResult(pipe, &ov, &n, TRUE))
      ok = false;
    data += n;
    len -= n;
  }
  CloseHandle(ov.hEvent);
  return ok;
}

HANDLE g_in = nullptr;
DWORD g_inMode = 0, g_outMode = 0;
UINT g_outCp = 0, g_inCp = 0;
void restoreConsole()
{
  if (!g_in)
    return;
  SetConsoleMode(g_in, g_inMode);
  SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), g_outMode);
  SetConsoleOutputCP(g_outCp);
  SetConsoleCP(g_inCp);
  g_in = nullptr;
}

} // namespace

QString shellProgram()
{
  for (const char *name : {"pwsh", "powershell"}) {
    const QString exe = QStandardPaths::findExecutable(QLatin1String(name));
    if (!exe.isEmpty())
      return exe;
  }
  return qEnvironmentVariable("ComSpec", QStringLiteral("cmd.exe"));
}

bool isPowerShell(const QString &program)
{
  const QString base = QFileInfo(program).baseName().toLower();
  return base == QLatin1String("pwsh") || base == QLatin1String("powershell");
}

QString commandLine(const QStringList &args)
{
  QStringList out;
  for (const QString &a : args) {
    if (!a.isEmpty() && !a.contains(QLatin1Char(' ')) && !a.contains(QLatin1Char('\t'))
        && !a.contains(QLatin1Char('"'))) {
      out << a;
      continue;
    }
    QString q = QStringLiteral("\"");
    int slashes = 0;
    for (const QChar c : a) {
      if (c == QLatin1Char('\\')) {
        ++slashes;
        continue;
      }
      // Backslashes count only before a quote: doubled there, and one
      // more to escape the quote itself.
      q += QString(c == QLatin1Char('"') ? slashes * 2 + 1 : slashes, QLatin1Char('\\'));
      q += c;
      slashes = 0;
    }
    q += QString(slashes * 2, QLatin1Char('\\'));
    q += QLatin1Char('"');
    out << q;
  }
  return out.join(QLatin1Char(' '));
}

QByteArray packet(PacketType type, const QByteArray &payload)
{
  QByteArray out;
  out.reserve(5 + payload.size());
  out.append(char(type));
  const quint32 len = quint32(payload.size());
  out.append(reinterpret_cast<const char *>(&len), 4);
  out.append(payload);
  return out;
}

// A pipe name rather than a path: \\.\pipe\ is where it lives.
QString newSocketPath()
{
  static int counter = 0;
  return QStringLiteral("syn-shell-%1-%2-%3")
    .arg(GetCurrentProcessId())
    .arg(++counter)
    .arg(QString::number(QRandomGenerator::global()->generate(), 36));
}

int runHolder(const QString &socketPath, const QString &cwd, int rows, int cols,
              const QStringList &command)
{
  int argc = 1;
  char name[] = "syn-shell-pty";
  char *argv[] = {name, nullptr};
  QCoreApplication app(argc, argv);

  QLocalServer server;
  server.setSocketOptions(QLocalServer::UserAccessOption); // this user only, as 0700 on Linux
  if (!server.listen(socketPath))
    return 1;

  HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
  if (!CreatePipe(&inRead, &inWrite, nullptr, 1 << 16) || !CreatePipe(&outRead, &outWrite, nullptr, 1 << 16))
    return 1;
  COORD size{SHORT(qMax(2, cols)), SHORT(qMax(2, rows))};
  HPCON pc = nullptr;
  if (FAILED(CreatePseudoConsole(size, inRead, outWrite, 0, &pc)))
    return 1;
  // The pseudoconsole has its own copies of its ends.
  CloseHandle(inRead);
  CloseHandle(outWrite);

  const QString cdFile = runtimeDir() + QLatin1Char('/') + socketPath + QStringLiteral(".cd");
  qputenv("SYN_SHELL", "1");
  qputenv("SYN_SHELL_CD_FILE", QDir::toNativeSeparators(cdFile).toUtf8());
  bool powershell = false;
  const HANDLE shell = startShell(pc, cwd, command, &powershell);
  if (!shell) {
    ClosePseudoConsole(pc);
    return 1;
  }

  QPointer<QLocalSocket> client;
  bool freshClient = false;
  bool closing = false;
  PacketReader reader;

  // Ends the session: the pseudoconsole closes, which the shell sees as
  // its console window closing, the same as a hangup.
  auto hangUp = [&] {
    if (closing)
      return;
    closing = true;
    ClosePseudoConsole(pc);
    if (WaitForSingleObject(shell, 2000) == WAIT_TIMEOUT)
      TerminateProcess(shell, 1);
    // The reader sees the end of the output and quits; this is in case it
    // doesn't.
    QTimer::singleShot(3000, &app, [] { QCoreApplication::quit(); });
  };

  // The pseudoconsole's output, read on a thread of its own (an anonymous
  // pipe can't be waited on), handed to whichever client is connected.
  const HANDLE readerDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  std::thread([outRead, readerDone, &app, &client] {
    char buf[65536];
    DWORD n = 0;
    while (ReadFile(outRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
      const QByteArray data(buf, int(n));
      QMetaObject::invokeMethod(&app, [&client, data] {
        if (client)
          client->write(data);
      }, Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(&app, [] { QCoreApplication::quit(); }, Qt::QueuedConnection);
    SetEvent(readerDone);
  }).detach();

  QWinEventNotifier exited(shell);
  QObject::connect(&exited, &QWinEventNotifier::activated, &app, [&] {
    // The shell exited: what it last wrote still comes through, then the
    // closed pseudoconsole ends the reader.
    if (!closing) {
      closing = true;
      ClosePseudoConsole(pc);
      QTimer::singleShot(3000, &app, [] { QCoreApplication::quit(); });
    }
  });

  auto onPackets = [&] {
    char type;
    QByteArray payload;
    while (reader.next(&type, &payload)) {
      if (type == Input) {
        DWORD n = 0;
        const char *p = payload.constData();
        DWORD left = DWORD(payload.size());
        while (left > 0 && WriteFile(inWrite, p, left, &n, nullptr) && n > 0) {
          p += n;
          left -= n;
        }
      } else if (type == Size) {
        const QList<QByteArray> parts = payload.split(' ');
        if (parts.size() == 2) {
          const COORD next{SHORT(qMax(2, parts[1].toInt())), SHORT(qMax(2, parts[0].toInt()))};
          // A client that just arrived has an empty screen: a size the
          // pseudoconsole isn't at makes it draw the whole screen again.
          if (freshClient && next.X == size.X && next.Y == size.Y)
            ResizePseudoConsole(pc, COORD{next.X, SHORT(qMax(2, next.Y - 1))});
          freshClient = false;
          size = next;
          ResizePseudoConsole(pc, size);
        }
      } else if (type == Cd && powershell) {
        QSaveFile f(cdFile);
        if (f.open(QIODevice::WriteOnly)) {
          f.write(QDir::toNativeSeparators(QString::fromUtf8(payload)).toUtf8());
          f.commit();
        }
      }
    }
  };

  QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
    while (QLocalSocket *s = server.nextPendingConnection()) {
      if (closing) {
        s->abort();
        s->deleteLater();
        continue;
      }
      // A new client takes the shell over; the old one is let go quietly.
      if (client) {
        QLocalSocket *old = client;
        old->disconnect(&app);
        old->abort();
        old->deleteLater();
      }
      client = s;
      freshClient = true;
      reader.buf.clear();
      QObject::connect(s, &QLocalSocket::readyRead, &app, [&, s] {
        if (s != client)
          return;
        reader.buf.append(s->readAll());
        onPackets();
      });
      QObject::connect(s, &QLocalSocket::disconnected, &app, [&, s] {
        if (s != client)
          return;
        // Gone with nobody taking over: hang the shell up, as closing a
        // terminal does.
        client = nullptr;
        s->deleteLater();
        hangUp();
      });
    }
  });

  app.exec();

  if (client) {
    client->flush();
    client->waitForBytesWritten(1000);
    client->disconnectFromServer();
  }
  if (!closing)
    ClosePseudoConsole(pc);
  if (WaitForSingleObject(shell, 0) == WAIT_TIMEOUT)
    TerminateProcess(shell, 1);
  CloseHandle(shell);
  CloseHandle(inWrite);
  QFile::remove(cdFile);
  // The reader posts to `app`, so it has to be done before `app` goes; if
  // it's stuck, there's nothing left to do but leave.
  if (WaitForSingleObject(readerDone, 3000) == WAIT_TIMEOUT)
    ExitProcess(0);
  return 0;
}

int runAttach(const QString &socketPath)
{
  const std::wstring name = wide(QStringLiteral("\\\\.\\pipe\\") + socketPath);
  const HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "syn-shell: no shell session at %s\n", socketPath.toLocal8Bit().constData());
    return 1;
  }

  // Raw: keys go to the shell as the terminal's own VT sequences, not
  // through the console's line editing, and its output is drawn as VT.
  const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (GetConsoleMode(in, &g_inMode) && GetConsoleMode(out, &g_outMode)) {
    g_in = in;
    g_outCp = GetConsoleOutputCP();
    g_inCp = GetConsoleCP();
    SetConsoleMode(in, ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT);
    SetConsoleMode(out, g_outMode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                          | DISABLE_NEWLINE_AUTO_RETURN);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    atexit(restoreConsole);
  }

  auto sendSize = [pipe, out] {
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(out, &info))
      return;
    const int rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    const int cols = info.srWindow.Right - info.srWindow.Left + 1;
    const QByteArray p = packet(Size, QByteArray::number(rows) + ' ' + QByteArray::number(cols));
    pipeWrite(pipe, p.constData(), DWORD(p.size()));
  };
  sendSize();

  // The shell's output, on a thread: when it ends (the shell exited, or
  // another client took it over) so does this.
  std::thread([pipe, out] {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    char buf[65536];
    for (;;) {
      DWORD n = 0;
      if (!ReadFile(pipe, buf, sizeof(buf), nullptr, &ov) && GetLastError() != ERROR_IO_PENDING)
        break;
      if (!GetOverlappedResult(pipe, &ov, &n, TRUE) || n == 0)
        break;
      DWORD written = 0;
      WriteFile(out, buf, n, &written, nullptr);
    }
    restoreConsole();
    ExitProcess(0);
  }).detach();

  INPUT_RECORD recs[128];
  for (;;) {
    DWORD n = 0;
    if (!ReadConsoleInputW(in, recs, 128, &n))
      break;
    std::wstring text;
    for (DWORD i = 0; i < n; ++i) {
      const INPUT_RECORD &r = recs[i];
      if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown && r.Event.KeyEvent.uChar.UnicodeChar) {
        for (WORD k = 0; k < qMax<WORD>(1, r.Event.KeyEvent.wRepeatCount); ++k)
          text += r.Event.KeyEvent.uChar.UnicodeChar;
      } else if (r.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        sendSize();
      }
    }
    if (text.empty())
      continue;
    const QByteArray p = packet(Input, QString::fromStdWString(text).toUtf8());
    if (!pipeWrite(pipe, p.constData(), DWORD(p.size())))
      break;
  }
  restoreConsole();
  return 0;
}

} // namespace PtySession
