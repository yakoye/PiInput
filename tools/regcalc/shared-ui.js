(function () {
  let toastTimer = null;

  function isEmbeddedTool() {
    try {
      return window.parent && window.parent !== window;
    } catch {
      return false;
    }
  }

  function notifyToolHeight() {
    if (!isEmbeddedTool()) return;
    const height = Math.ceil(Math.max(
      document.body ? document.body.scrollHeight : 0,
      document.documentElement ? document.documentElement.scrollHeight : 0
    ));
    window.parent.postMessage({ type: "tool-resize", height }, "*");
  }

  if (isEmbeddedTool()) {
    document.documentElement.classList.add("embedded-tool");
    document.addEventListener("DOMContentLoaded", () => {
      document.body.classList.add("embedded-tool");
      notifyToolHeight();
      if ("ResizeObserver" in window) {
        const observer = new ResizeObserver(notifyToolHeight);
        observer.observe(document.body);
      }
    });
    window.addEventListener("load", notifyToolHeight);
    window.addEventListener("resize", notifyToolHeight);
    window.setTimeout(notifyToolHeight, 250);
    window.setTimeout(notifyToolHeight, 1000);
  }

  window.toolboxToast = function toolboxToast(message) {
    let toast = document.querySelector(".toolbox-toast");
    if (!toast) {
      toast = document.createElement("div");
      toast.className = "toolbox-toast";
      document.body.appendChild(toast);
    }
    toast.textContent = message;
    toast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("show"), 2000);
  };

  window.toolboxCopyText = async function toolboxCopyText(text, okMessage) {
    if (!text) return false;
    let copied = false;
    try {
      await navigator.clipboard.writeText(text);
      copied = true;
    } catch {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.setAttribute("readonly", "");
      textarea.style.position = "fixed";
      textarea.style.left = "-9999px";
      document.body.appendChild(textarea);
      textarea.select();
      try {
        copied = document.execCommand("copy") === true;
      } catch {
        copied = false;
      } finally {
        document.body.removeChild(textarea);
      }
    }
    if (!copied) {
      window.toolboxToast("复制失败");
      return false;
    }
    window.toolboxToast(okMessage || "已复制");
    return true;
  };
})();
