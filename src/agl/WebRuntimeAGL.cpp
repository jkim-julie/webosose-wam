#include "WebRuntimeAGL.h"

#include <cassert>
#include <netinet/in.h>
#include <regex>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libxml/parser.h>

#include <webos/app/webos_main.h>

#include "LogManager.h"
#include "PlatformModuleFactoryImpl.h"
#include "WebAppManager.h"
#include "WebAppManagerServiceAGL.h"


#define WEBAPP_CONFIG "config.xml"

volatile sig_atomic_t e_flag = 0;


static std::string getAppId(const std::vector<std::string>& args) {
  const char *afm_id = getenv("AFM_ID");
  if (afm_id == nullptr || !afm_id[0]) {
    return args[0];
  } else {
    return std::string(afm_id);
  }
}

static std::string getAppUrl(const std::vector<std::string>& args) {
  for (size_t i=0; i < args.size(); i++) {
    std::size_t found = args[i].find(std::string("http://"));
    if (found != std::string::npos)
        return args[i];
  }
  return std::string();
}

static bool isBrowserProcess(const std::vector<std::string>& args) {
  // if type is not given then we are browser process
  for (size_t i=0; i < args.size(); i++) {
    std::string param("--type=");
    std::size_t found = args[i].find(param);
    if (found != std::string::npos)
        return false;
  }
  return true;
}

static bool isSharedBrowserProcess(const std::vector<std::string>& args) {
  // if 'http://' param is not present then assume shared browser process
  for (size_t i=0; i < args.size(); i++) {
    std::size_t found = args[i].find(std::string("http://"));
    if (found != std::string::npos)
        return false;
  }
  return true;
}

class AGLMainDelegateWAM : public webos::WebOSMainDelegate {
public:
    void AboutToCreateContentBrowserClient() override {
      WebAppManagerServiceAGL::instance()->startService();
      WebAppManager::instance()->setPlatformModules(new PlatformModuleFactoryImpl());
    }
};

class AGLRendererDelegateWAM : public webos::WebOSMainDelegate {
public:
    void AboutToCreateContentBrowserClient() override {
      // do nothing
    }
};

void Launcher::register_surfpid(pid_t app_pid, pid_t surf_pid)
{
  if (app_pid != m_rid)
    return;
  bool result = m_pid_map.insert({app_pid, surf_pid}).second;
  if (!result) {
    fprintf(stderr, "register_surfpid, (app_pid=%d) already registered surface_id with (surface_id=%d)\r\n",
            (int)app_pid, (int)surf_pid);
  }
}

void Launcher::unregister_surfpid(pid_t app_pid, pid_t surf_pid)
{
  size_t erased_count = m_pid_map.erase(app_pid);
  if (erased_count == 0) {
    fprintf(stderr, "unregister_surfpid, (app_pid=%d) doesn't have a registered surface\r\n",
            (int)app_pid);
  }
}

pid_t Launcher::find_surfpid_by_rid(pid_t app_pid)
{
  auto surface_id = m_pid_map.find(app_pid);
  if (surface_id != m_pid_map.end()) {
    fprintf(stderr, "found return(%d, %d)\r\n", (int)app_pid, (int)surface_id->second);
    return surface_id->second;
  }
  return -1;
}

int SingleBrowserProcessWebAppLauncher::launch(const std::string& id, const std::string& uri) {
  m_rid = (int)getpid();
  WebAppManagerServiceAGL::instance()->setStartupApplication(id, uri, m_rid);
  return m_rid;
}

int SingleBrowserProcessWebAppLauncher::loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

int SharedBrowserProcessWebAppLauncher::launch(const std::string& id, const std::string& uri) {
  if (!WebAppManagerServiceAGL::instance()->initializeAsHostClient()) {
    fprintf(stderr,"Failed to initialize as host client\r\n");
    return -1;
  }

  tiny_proxy_ = std::make_unique<TinyProxy>();
  int port = tiny_proxy_->port();
  std::string proxy_rules = std::string("localhost");
  proxy_rules.append(":");
  proxy_rules.append(std::to_string(port));

  m_rid = (int)getpid();
  std::string m_rid_s = std::to_string(m_rid);
  std::vector<const char*> data;
  data.push_back(id.c_str());
  data.push_back(uri.c_str());
  data.push_back(m_rid_s.c_str());
  data.push_back(proxy_rules.c_str());

  WebAppManagerServiceAGL::instance()->launchOnHost(data.size(), data.data());
  return m_rid;
}

int SharedBrowserProcessWebAppLauncher::loop(int argc, const char** argv, volatile sig_atomic_t& e_flag) {
  // TODO: wait for a pid
  while (1)
    sleep(1);
  return 0;
}

int WebAppLauncherRuntime::run(int argc, const char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  m_id = getAppId(args);
  m_url = getAppUrl(args);
  m_role = "WebApp";

  if(WebAppManagerServiceAGL::instance()->isHostServiceRunning()) {
    fprintf(stderr, "WebAppLauncherRuntime::run - creating SharedBrowserProcessWebAppLauncher\r\n");
    m_launcher = new SharedBrowserProcessWebAppLauncher();
  } else {
    fprintf(stderr, "WebAppLauncherRuntime::run - creating SingleBrowserProcessWebAppLauncher\r\n");
    m_launcher = new SingleBrowserProcessWebAppLauncher();
  }


  // Initialize SIGTERM handler
  // TODO: init_signal();

  if (!init())
    return -1;

  /* Launch WAM application */
  m_launcher->m_rid = m_launcher->launch(m_id, m_url);

  if (m_launcher->m_rid < 0) {
    fprintf(stderr, "cannot launch WAM app (%s)\r\n", m_id.c_str());
  }

  // take care 1st time launch
  fprintf(stderr, "waiting for notification: surface created\r\n");

  return m_launcher->loop(argc, argv, e_flag);
}

void WebAppLauncherRuntime::OnActive() {
  fprintf(stderr, "WebAppLauncherRuntime::OnActive\r\n");
}

void WebAppLauncherRuntime::OnInactive() {
  fprintf(stderr, "WebAppLauncherRuntime::OnInactive\r\n");
}

void WebAppLauncherRuntime::OnVisible() {
  fprintf(stderr, "WebAppLauncherRuntime::OnVisible\r\n");
}

void WebAppLauncherRuntime::OnInvisible() {
  fprintf(stderr, "WebAppLauncherRuntime::OnInvisible\r\n");
}

void WebAppLauncherRuntime::OnSyncDraw() {
  fprintf(stderr, "WebAppLauncherRuntime::OnSyncDraw\r\n");
}

void WebAppLauncherRuntime::OnFlushDraw() {
  fprintf(stderr, "WebAppLauncherRuntime::OnFlushDraw\r\n");
}

void WebAppLauncherRuntime::OnTabShortcut() {
  fprintf(stderr, "WebAppLauncherRuntime::OnTabShortcut\r\n");
}

void WebAppLauncherRuntime::OnScreenMessage(const char* message) {
  fprintf(stderr, "WebAppLauncherRuntime::OnScreenMessage:%s\r\n", message);
}

void WebAppLauncherRuntime::OnSurfaceCreated(int id, pid_t pid) {
  fprintf(stderr, "WebAppLauncherRuntime::OnSurfaceCreated\r\n");
  m_launcher->register_surfpid(id, pid);
}

void WebAppLauncherRuntime::OnSurfaceDestroyed(int id, pid_t pid) {
  fprintf(stderr, "WebAppLauncherRuntime::OnSurfaceDestroyed\r\n");
  m_launcher->unregister_surfpid(id, pid);
}

void WebAppLauncherRuntime::OnRequestedSurfaceID(int id, pid_t* surface_pid_output) {
  *surface_pid_output = m_launcher->find_surfpid_by_rid(id);
}

bool WebAppLauncherRuntime::init() {
  // based on https://tools.ietf.org/html/rfc3986#page-50
  std::regex url_regex (
    R"(^(([^:\/?#]+):)?(//([^\/?#]*))?([^?#]*)(\?([^#]*))?(#(.*))?)",
    std::regex::extended
  );

  std::smatch url_match_result;
  if (std::regex_match(m_url, url_match_result, url_regex)) {
    unsigned counter = 0;
    for (const auto& res : url_match_result) {
      fprintf(stderr, "    %d: %s\r\n", counter++, res.str().c_str());
    }

    if (url_match_result.size() > 4) {
      std::string authority = url_match_result[4].str();
      std::size_t n = authority.find(':');
      if (n != std::string::npos) {
        std::string sport = authority.substr(n+1);
        m_role.append("-");
        m_role.append(sport);
        m_port = std::stoi(sport);
      }
    }

    if (url_match_result.size() > 7) {
      std::string query = url_match_result[7].str();
      std::size_t n = query.find('=');
      if (n != std::string::npos) {
        m_token = query.substr(n+1);
      }
    }

    auto path = std::string(getenv("AFM_APP_INSTALL_DIR"));
    path = path + "/" + WEBAPP_CONFIG;

    // Parse config file of runxdg
    if (parse_config(path.c_str())) {
      fprintf(stderr, "Error in config\r\n");
      return false;
    }

    fprintf(stderr, "id=[%s], name=[%s], role=[%s], url=[%s], port=%d, token=[%s]\r\n",
            m_id.c_str(), m_name.c_str(), m_role.c_str(), m_url.c_str(),
            m_port, m_token.c_str());

    m_app_bridge = std::make_unique<AppBridge>(m_port, m_token, m_id, m_role, this);
    m_app_bridge->SetName(m_name);

    return true;
  } else {
    fprintf(stderr, "Malformed url.\r\n");
    return false;
  }
}

int WebAppLauncherRuntime::parse_config (const char *path_to_config)
{
  xmlDoc *doc = xmlReadFile(path_to_config, nullptr, 0);
  xmlNode *root = xmlDocGetRootElement(doc);

  xmlChar *id = nullptr;
  xmlChar *version = nullptr;
  xmlChar *name = nullptr;
  xmlChar *content = nullptr;
  xmlChar *description = nullptr;
  xmlChar *author = nullptr;
  xmlChar *icon = nullptr;

  id = xmlGetProp(root, (const xmlChar*)"id");
  version = xmlGetProp(root, (const xmlChar*)"version");
  for (xmlNode *node = root->children; node; node = node->next) {
    if (!xmlStrcmp(node->name, (const xmlChar*)"name"))
      name = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!xmlStrcmp(node->name, (const xmlChar*)"icon"))
      icon = xmlGetProp(node, (const xmlChar*)"src");
    if (!xmlStrcmp(node->name, (const xmlChar*)"content"))
      content = xmlGetProp(node, (const xmlChar*)"src");
    if (!xmlStrcmp(node->name, (const xmlChar*)"description"))
      description = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
    if (!xmlStrcmp(node->name, (const xmlChar*)"author"))
      author = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
  }
  fprintf(stdout, "...parse_config...\n");
  fprintf(stderr, "id: %s\r\n", id);
  fprintf(stderr, "version: %s\r\n", version);
  fprintf(stderr, "name: %s\r\n", name);
  fprintf(stderr, "content: %s\r\n", content);
  fprintf(stderr, "description: %s\r\n", description);
  fprintf(stderr, "author: %s\r\n", author);
  fprintf(stderr, "icon: %s\r\n", icon);

  m_name = std::string((const char*)name);

  xmlFree(id);
  xmlFree(version);
  xmlFree(name);
  xmlFree(content);
  xmlFree(description);
  xmlFree(author);
  xmlFree(icon);
  xmlFreeDoc(doc);

  return 0;
}


int SharedBrowserProcessRuntime::run(int argc, const char** argv) {
  if (WebAppManagerServiceAGL::instance()->initializeAsHostService()) {
    AGLMainDelegateWAM delegate;
    webos::WebOSMain webOSMain(&delegate);
    return webOSMain.Run(argc, argv);
  } else {
    fprintf(stderr, "Trying to start shared browser process but process is already running\r\n");
    return -1;
  }
}

int RenderProcessRuntime::run(int argc, const char** argv) {
  AGLMainDelegateWAM delegate;
  webos::WebOSMain webOSMain(&delegate);
  return webOSMain.Run(argc, argv);
}

int WebRuntimeAGL::run(int argc, const char** argv) {
  fprintf(stderr, "WebRuntimeAGL::run\r\n");
  std::vector<std::string> args(argv + 1, argv + argc);
  if (isBrowserProcess(args)) {
    if (isSharedBrowserProcess(args)) {
      fprintf(stderr, "WebRuntimeAGL - creating SharedBrowserProcessRuntime\r\n");
      m_runtime = new SharedBrowserProcessRuntime();
    }  else {
      fprintf(stderr, "WebRuntimeAGL - creating WebAppLauncherRuntime\r\n");
      m_runtime = new WebAppLauncherRuntime();
    }
  } else {
    fprintf(stderr, "WebRuntimeAGL - creating RenderProcessRuntime\r\n");
    m_runtime = new RenderProcessRuntime();
  }

  return m_runtime->run(argc, argv);
}

TinyProxy::TinyProxy() {
  // Get a free port to listen
  int test_socket = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in test_socket_addr;
  memset(&test_socket_addr, 0, sizeof(test_socket_addr));
  test_socket_addr.sin_port = htons(0);
  test_socket_addr.sin_family = AF_INET;
  bind(test_socket, (struct sockaddr*) &test_socket_addr, sizeof(struct sockaddr_in));
  socklen_t len = sizeof(test_socket_addr);
  getsockname(test_socket, (struct sockaddr*) &test_socket_addr, &len);
  int port = ntohs(test_socket_addr.sin_port);
  close(test_socket);

  setPort(port);

  std::string cmd = "tinyproxy -p " + std::to_string(port);
  int res = std::system(cmd.data());
  if (res == -1)
    fprintf(stderr, "Error while running %s\r\n", cmd.data());
}
