#ifndef WEBRUNTIME_AGL_H
#define WEBRUNTIME_AGL_H

#include <map>
#include <memory>
#include <signal.h>
#include <string>
#include <unordered_map>

#include <libappbridge.h>

#include "WebRuntime.h"

class Launcher {
public:
  virtual void register_surfpid(pid_t app_pid, pid_t surf_pid);
  virtual void unregister_surfpid(pid_t app_pid, pid_t surf_pid);
  virtual pid_t find_surfpid_by_rid(pid_t app_pid);
  virtual int launch(const std::string& id, const std::string& uri) = 0;
  virtual int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) = 0;

  int m_rid = 0;
  std::unordered_map<pid_t, pid_t> m_pid_map; // pair of <app_pid, pid which creates a surface>
};

class TinyProxy {
 public:
  int port() { return port_; }
  void setPort(int port) { port_ = port; }

  TinyProxy();
private:
  int port_;
};

class SharedBrowserProcessWebAppLauncher : public Launcher {
public:
  int launch(const std::string& id, const std::string& uri) override;
  int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
private:
  std::unique_ptr<TinyProxy> tiny_proxy_;
};

class SingleBrowserProcessWebAppLauncher : public Launcher {
public:
  int launch(const std::string& id, const std::string& uri) override;
  int loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) override;
};

class WebAppLauncherRuntime  : public WebRuntime,
                               public AppBridgeDelegate {
public:
  int run(int argc, const char** argv) override;

  // AppBridgeDelegate:
  void OnActive() override;
  void OnInactive() override;
  void OnVisible() override;
  void OnInvisible() override;
  void OnSyncDraw() override;
  void OnFlushDraw() override;
  void OnTabShortcut() override;
  void OnScreenMessage(const char* message) override;
  void OnSurfaceCreated(int id, pid_t pid) override;
  void OnSurfaceDestroyed(int id, pid_t pid) override;
  void OnRequestedSurfaceID(int id, pid_t* surface_pid_output) override;

private:

  bool init();
  int parse_config(const char *file);

  std::string m_id;
  std::string m_role;
  std::string m_url;
  std::string m_name;

  int m_port;
  std::string m_token;

  Launcher *m_launcher;
  std::unique_ptr<AppBridge> m_app_bridge;
};

class SharedBrowserProcessRuntime  : public WebRuntime {
public:
  int run(int argc, const char** argv) override;
};

class RenderProcessRuntime  : public WebRuntime {
public:
  int run(int argc, const char** argv) override;
};

class WebRuntimeAGL : public WebRuntime {
public:
  int run(int argc, const char** argv) override;

private:

  WebRuntime *m_runtime;
};

#endif // WEBRUNTIME_AGL_H
