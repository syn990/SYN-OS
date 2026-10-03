#include "PtySession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace PtySession {

namespace {

// Written fresh by every holder; ZDOTDIR points the shell at it.
const char kZshEnv[] = R"ZSH(# SYN-SHELL's zsh integration. Put ZDOTDIR back, run the user's own
# startup files as usual, then hook in.
if [[ -n ${SYN_SHELL_ZDOTDIR+x} ]]; then ZDOTDIR=$SYN_SHELL_ZDOTDIR; else unset ZDOTDIR; fi
unset SYN_SHELL_ZDOTDIR
[[ -f ${ZDOTDIR:-$HOME}/.zshenv ]] && builtin source ${ZDOTDIR:-$HOME}/.zshenv
if [[ -o interactive ]]; then
  autoload -Uz add-zsh-hook
  # OSC 7: tell the browser where the shell is now.
  _syn_shell_osc7() { builtin print -n "\e]7;file://${HOST}${PWD//\%/%25}\a" }
  add-zsh-hook precmd _syn_shell_osc7
  add-zsh-hook chpwd _syn_shell_osc7
  # The browser moved: follow it, keep whatever is typed on the line.
  TRAPUSR1() {
    local d
    d=$(<$SYN_SHELL_CD_FILE) 2>/dev/null || return 0
    [[ -d $d && $d != $PWD ]] || return 0
    builtin cd -q -- $d || return 0
    _syn_shell_osc7
    zle && zle reset-prompt
    return 0
  }
fi
)ZSH";

const char kBashRc[] = R"BASH(# SYN-SHELL's bash integration: the user's bashrc, then OSC 7.
[[ -f ~/.bashrc ]] && source ~/.bashrc
_syn_shell_osc7() { printf '\e]7;file://%s%s\a' "$HOSTNAME" "${PWD//\%/%25}"; }
PROMPT_COMMAND="_syn_shell_osc7${PROMPT_COMMAND:+;$PROMPT_COMMAND}"
)BASH";

QString runtimeDir()
{
  const QByteArray xdg = qgetenv("XDG_RUNTIME_DIR");
  const QString base = xdg.isEmpty() ? QStringLiteral("/tmp/syn-shell-%1").arg(getuid())
                                     : QString::fromLocal8Bit(xdg) + QStringLiteral("/syn-shell");
  QDir().mkpath(base);
  chmod(QFile::encodeName(base).constData(), 0700);
  return base;
}

bool writeAll(int fd, const char *data, size_t len)
{
  while (len > 0) {
    const ssize_t n = write(fd, data, len);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
        pollfd p{fd, POLLOUT, 0};
        poll(&p, 1, 1000);
        continue;
      }
      return false;
    }
    data += n;
    len -= size_t(n);
  }
  return true;
}

int listenOn(const QString &path)
{
  const QByteArray p = QFile::encodeName(path);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (size_t(p.size()) >= sizeof(addr.sun_path))
    return -1;
  memcpy(addr.sun_path, p.constData(), size_t(p.size()));
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  unlink(p.constData());
  const mode_t old = umask(0077);
  const int bound = bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
  umask(old);
  if (bound < 0 || listen(fd, 4) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int connectTo(const QString &path)
{
  const QByteArray p = QFile::encodeName(path);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (size_t(p.size()) >= sizeof(addr.sun_path))
    return -1;
  memcpy(addr.sun_path, p.constData(), size_t(p.size()));
  const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return -1;
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

// Starts the user's shell on a new pty, with the integration for zsh or
// bash. Returns the shell's pid; *master is the pty's master side.
pid_t startShell(const QString &cwd, int rows, int cols, const QString &cdFile, int *master)
{
  QByteArray shell = qgetenv("SHELL");
  if (shell.isEmpty() || access(shell.constData(), X_OK) != 0)
    shell = "/bin/sh";
  const QByteArray name = QFileInfo(QString::fromLocal8Bit(shell)).fileName().toLocal8Bit();

  const QString integration = runtimeDir() + QStringLiteral("/integration");
  QDir().mkpath(integration + QStringLiteral("/zsh"));
  QFile zshenv(integration + QStringLiteral("/zsh/.zshenv"));
  if (zshenv.open(QIODevice::WriteOnly | QIODevice::Truncate))
    zshenv.write(kZshEnv);
  zshenv.close();
  QFile bashrc(integration + QStringLiteral("/bashrc"));
  if (bashrc.open(QIODevice::WriteOnly | QIODevice::Truncate))
    bashrc.write(kBashRc);
  bashrc.close();

  winsize ws{};
  ws.ws_row = static_cast<unsigned short>(rows);
  ws.ws_col = static_cast<unsigned short>(cols);
  const pid_t pid = forkpty(master, nullptr, nullptr, &ws);
  if (pid != 0)
    return pid;

  // Child: the shell.
  signal(SIGPIPE, SIG_DFL);
  signal(SIGHUP, SIG_DFL);
  if (chdir(QFile::encodeName(cwd).constData()) != 0)
    (void)!chdir(getenv("HOME") ? getenv("HOME") : "/");
  setenv("TERM", "xterm-256color", 1);
  setenv("COLORTERM", "truecolor", 1);
  setenv("SYN_SHELL", "1", 1);
  setenv("SYN_SHELL_CD_FILE", QFile::encodeName(cdFile).constData(), 1);
  if (name == "zsh") {
    if (const char *z = getenv("ZDOTDIR"))
      setenv("SYN_SHELL_ZDOTDIR", z, 1);
    setenv("ZDOTDIR", QFile::encodeName(integration + QStringLiteral("/zsh")).constData(), 1);
    execl(shell.constData(), name.constData(), "-i", static_cast<char *>(nullptr));
  } else if (name == "bash") {
    const QByteArray rc = QFile::encodeName(integration + QStringLiteral("/bashrc"));
    execl(shell.constData(), name.constData(), "--rcfile", rc.constData(), "-i",
          static_cast<char *>(nullptr));
  } else {
    execl(shell.constData(), name.constData(), "-i", static_cast<char *>(nullptr));
  }
  _exit(127);
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

volatile sig_atomic_t g_winch = 0;
void onWinch(int) { g_winch = 1; }

termios g_saved;
bool g_raw = false;
void restoreTty()
{
  if (g_raw)
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved);
  g_raw = false;
}

} // namespace

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

QString newSocketPath()
{
  static int counter = 0;
  return runtimeDir() + QStringLiteral("/%1-%2-%3.sock")
                          .arg(getpid())
                          .arg(++counter)
                          .arg(QString::number(quint32(random()), 36));
}

int runHolder(const QString &socketPath, const QString &cwd, int rows, int cols)
{
  setsid();
  signal(SIGPIPE, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  prctl(PR_SET_NAME, "syn-shell-pty", 0, 0, 0);

  const QString socketDir = QFileInfo(socketPath).absolutePath();
  QDir().mkpath(socketDir);
  chmod(QFile::encodeName(socketDir).constData(), 0700);
  const int lfd = listenOn(socketPath);
  if (lfd < 0)
    return 1;
  const QString cdFile = socketPath + QStringLiteral(".cd");
  int master = -1;
  const pid_t shell = startShell(cwd, qMax(2, rows), qMax(2, cols), cdFile, &master);
  if (shell < 0) {
    unlink(QFile::encodeName(socketPath).constData());
    return 1;
  }
  fcntl(master, F_SETFD, FD_CLOEXEC);
  const bool zsh = QFileInfo(QString::fromLocal8Bit(qgetenv("SHELL"))).fileName()
                   == QLatin1String("zsh");

  int client = -1;
  bool freshClient = false;
  PacketReader reader;
  char buf[65536];

  auto dropClient = [&] {
    if (client >= 0)
      close(client);
    client = -1;
    reader.buf.clear();
  };

  for (;;) {
    pollfd fds[3] = {{lfd, POLLIN, 0}, {master, POLLIN, 0}, {client, POLLIN, 0}};
    if (poll(fds, client >= 0 ? 3 : 2, -1) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (fds[0].revents & POLLIN) {
      const int fd = accept4(lfd, nullptr, nullptr, SOCK_CLOEXEC);
      if (fd >= 0) {
        dropClient(); // a new client takes the shell over
        client = fd;
        freshClient = true;
      }
    }

    if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      const ssize_t n = read(master, buf, sizeof(buf));
      if (n <= 0 && !(n < 0 && errno == EINTR))
        break; // the shell exited
      if (n > 0 && client >= 0 && !writeAll(client, buf, size_t(n)))
        dropClient();
    }

    if (client >= 0 && (fds[2].revents & (POLLIN | POLLHUP | POLLERR))) {
      const ssize_t n = read(client, buf, sizeof(buf));
      if (n <= 0) {
        // Gone with nobody taking over: hang the shell up, as closing a
        // terminal does.
        dropClient();
        break;
      }
      reader.buf.append(buf, n);
      char type;
      QByteArray payload;
      while (reader.next(&type, &payload)) {
        if (type == Input) {
          writeAll(master, payload.constData(), size_t(payload.size()));
        } else if (type == Size) {
          const QList<QByteArray> parts = payload.split(' ');
          if (parts.size() == 2) {
            winsize ws{};
            ws.ws_row = static_cast<unsigned short>(qMax(2, parts[0].toInt()));
            ws.ws_col = static_cast<unsigned short>(qMax(2, parts[1].toInt()));
            ioctl(master, TIOCSWINSZ, &ws);
            // A client that just arrived has an empty screen: make
            // whatever is in the foreground draw itself again, even if
            // the size didn't change.
            if (freshClient) {
              const pid_t fg = tcgetpgrp(master);
              if (fg > 0)
                kill(-fg, SIGWINCH);
              freshClient = false;
            }
          }
        } else if (type == Cd && zsh && tcgetpgrp(master) == shell) {
          QFile f(cdFile);
          if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(payload);
            f.close();
            kill(shell, SIGUSR1);
          }
        }
      }
    }
  }

  close(lfd);
  unlink(QFile::encodeName(socketPath).constData());
  unlink(QFile::encodeName(cdFile).constData());
  kill(shell, SIGHUP);
  close(master);
  for (int i = 0; i < 20 && waitpid(shell, nullptr, WNOHANG) == 0; ++i)
    usleep(50000);
  if (waitpid(shell, nullptr, WNOHANG) == 0) {
    kill(shell, SIGKILL);
    waitpid(shell, nullptr, 0);
  }
  return 0;
}

int runAttach(const QString &socketPath)
{
  const int fd = connectTo(socketPath);
  if (fd < 0) {
    fprintf(stderr, "syn-shell: no shell session at %s\n", QFile::encodeName(socketPath).constData());
    return 1;
  }

  if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &g_saved) == 0) {
    termios raw = g_saved;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw = true;
    atexit(restoreTty);
  }
  struct sigaction sa{};
  sa.sa_handler = onWinch;
  sigaction(SIGWINCH, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  auto sendSize = [fd] {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row && ws.ws_col) {
      const QByteArray p = packet(Size, QByteArray::number(ws.ws_row) + ' '
                                          + QByteArray::number(ws.ws_col));
      writeAll(fd, p.constData(), size_t(p.size()));
    }
  };
  sendSize();

  char buf[65536];
  for (;;) {
    if (g_winch) {
      g_winch = 0;
      sendSize();
    }
    pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {fd, POLLIN, 0}};
    if (poll(fds, 2, -1) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (fds[0].revents & (POLLIN | POLLHUP)) {
      const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n <= 0)
        break;
      const QByteArray p = packet(Input, QByteArray(buf, n));
      if (!writeAll(fd, p.constData(), size_t(p.size())))
        break;
    }
    if (fds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
      const ssize_t n = read(fd, buf, sizeof(buf));
      if (n <= 0)
        break; // the shell ended, or another client took it
      writeAll(STDOUT_FILENO, buf, size_t(n));
    }
  }
  restoreTty();
  close(fd);
  return 0;
}

} // namespace PtySession
