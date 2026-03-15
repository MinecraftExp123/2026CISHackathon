/* ══════════════════════════════════════════════════════════════════════════
   MacroForge — script.js
   Extracted from webpage.html and organised with section comments.
   Companion files: styles.css, webpage.html
   ══════════════════════════════════════════════════════════════════════════ */

/* ════════════════════════════════════════════════════════════════════════════
   ── DARK MODE
   Checks the OS-level colour-scheme preference on load and keeps it in sync
   via a MediaQueryList change listener. Toggles the .dark class on <html>.
   ════════════════════════════════════════════════════════════════════════════ */
if (
  window.matchMedia &&
  window.matchMedia("(prefers-color-scheme: dark)").matches
) {
  document.documentElement.classList.add("dark");
}
window
  .matchMedia("(prefers-color-scheme: dark)")
  .addEventListener("change", function (e) {
    document.documentElement.classList.toggle("dark", e.matches);
  });

/* ════════════════════════════════════════════════════════════════════════════
   ── STATE
   Single flat state object shared by all rendering functions.
   nextId is a monotonically incrementing counter used to assign unique
   IDs to newly created macro objects.
   ════════════════════════════════════════════════════════════════════════════ */
var nextId = 0;
var state = {
  macros: [], // Array of macro objects currently loaded
  search: "", // Current search query string
  aiMode: "generate", // "generate" | "explain"
  aiLoading: false, // True while an AI request is in-flight
  aiResult: null, // Last AI result payload (or null)
  aiCount: 0, // Total number of successful AI calls
  expFmt: "shell", // Export format: "shell" | "csv" | "json"
  expSel: new Set(), // Set of macro IDs selected for export
  guideCollapsed: false, // Whether the how-to-use guide is collapsed
};

/* ════════════════════════════════════════════════════════════════════════════
   ── HELPERS
   Small utility functions used throughout the application.
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * esc — Safely HTML-encode a string to prevent XSS when building innerHTML.
 * @param {string} s - Raw string to encode.
 * @returns {string} HTML-escaped string.
 */
function esc(s) {
  var d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

/**
 * mdRender — Renders a Markdown string to sanitised HTML.
 * Uses marked (parser) + DOMPurify (sanitiser) when both are available,
 * falling back to plain-text with <br> line breaks.
 * @param {string} md - Markdown source string.
 * @returns {string} Safe HTML string.
 */
function mdRender(md) {
  if (typeof marked !== "undefined" && typeof DOMPurify !== "undefined")
    return DOMPurify.sanitize(marked.parse(md));
  return esc(md).replace(/\n/g, "<br>");
}

/**
 * toast — Shows a temporary slide-in notification in the top-right corner.
 * @param {string} msg  - Message to display.
 * @param {string} type - Severity: "ok" | "err" | "info" (default "ok").
 */
function toast(msg, type) {
  type = type || "ok";
  var icons = {
    ok: "fa-check-circle",
    err: "fa-times-circle",
    info: "fa-info-circle",
  };
  var el = document.createElement("div");
  el.className = "toast toast-" + type;
  el.innerHTML =
    '<i class="fas ' +
    (icons[type] || icons.info) +
    '"></i><span>' +
    esc(msg) +
    "</span>";
  document.getElementById("toasts").appendChild(el);
  setTimeout(function () {
    el.classList.add("out");
    setTimeout(function () {
      el.remove();
    }, 250);
  }, 2800);
}

/**
 * confirm2 — Renders a modal confirmation dialog.
 * Calls onYes() if the user confirms, removes the overlay on cancel or
 * on backdrop click.
 * @param {string}   msg   - Confirmation message to display.
 * @param {function} onYes - Callback executed when the user confirms.
 */
function confirm2(msg, onYes) {
  var trigger = document.activeElement;
  var ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML =
    '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="cf-title" style="max-width:360px;"><div class="confirm-box">' +
    '<i class="fas fa-exclamation-triangle" aria-hidden="true"></i>' +
    '<p id="cf-title">' +
    esc(msg) +
    "</p>" +
    '<div class="confirm-btns"><button class="btn btn-ghost" id="cf-no">Cancel</button><button class="btn btn-danger" id="cf-yes">Delete</button></div>' +
    "</div></div>";
  document.getElementById("modal-root").appendChild(ov);
  var removeTrap = trapFocus(ov.querySelector(".modal"));
  ov.querySelector("#cf-no").onclick = function () {
    removeTrap();
    ov.remove();
    if (trigger && trigger.focus) trigger.focus();
  };
  ov.querySelector("#cf-yes").onclick = function () {
    removeTrap();
    ov.remove();
    if (trigger && trigger.focus) trigger.focus();
    onYes();
  };
  ov.addEventListener("click", function (e) {
    if (e.target === ov) {
      removeTrap();
      ov.remove();
      if (trigger && trigger.focus) trigger.focus();
    }
  });
  ov.addEventListener("keydown", function (e) {
    if (e.key === "Escape") {
      removeTrap();
      ov.remove();
      if (trigger && trigger.focus) trigger.focus();
    }
  });
}

/**
 * trapFocus — Constrains Tab/Shift+Tab keyboard navigation inside a container.
 * Moves focus to the first focusable element immediately.
 * Returns a cleanup function that removes the event listener.
 * @param {HTMLElement} container - The element to trap focus within.
 * @returns {function} cleanup - Call to remove the trap.
 */
function trapFocus(container) {
  var selector =
    'button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
  function getFocusable() {
    return Array.prototype.slice.call(container.querySelectorAll(selector));
  }
  function handler(e) {
    if (e.key !== "Tab") return;
    var focusable = getFocusable();
    if (!focusable.length) {
      e.preventDefault();
      return;
    }
    var first = focusable[0];
    var last = focusable[focusable.length - 1];
    if (e.shiftKey) {
      if (document.activeElement === first) {
        e.preventDefault();
        last.focus();
      }
    } else {
      if (document.activeElement === last) {
        e.preventDefault();
        first.focus();
      }
    }
  }
  container.addEventListener("keydown", handler);
  var focusable = getFocusable();
  if (focusable.length) focusable[0].focus();
  return function () {
    container.removeEventListener("keydown", handler);
  };
}

/* ════════════════════════════════════════════════════════════════════════════
   ── GUIDE TOGGLE
   Collapse / expand the How-to-Use guide card. Toggles the
   .guide-collapsed class which hides .guide-steps via CSS.
   ════════════════════════════════════════════════════════════════════════════ */
document.getElementById("guide-toggle").addEventListener("click", function () {
  var card = document.getElementById("guide-card");
  state.guideCollapsed = !state.guideCollapsed;
  card.classList.toggle("guide-collapsed", state.guideCollapsed);
  var span = this.querySelector("span");
  span.textContent = state.guideCollapsed ? "Show guide" : "Hide guide";
  this.setAttribute("aria-expanded", state.guideCollapsed ? "false" : "true");
});

/* ════════════════════════════════════════════════════════════════════════════
   ── FILE PARSING
   Auto-detects and parses CSV, JSON, or Shell alias file content into an
   array of normalised macro objects.

   Detection order:
     1. JSON  — starts with "[" or "{"
     2. Shell — contains "alias name='command'" patterns
     3. CSV   — falls through to comma-separated line parsing

   Each returned macro has the shape:
   { id, name, command, keybinding, category, description }
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * parseFileContent — Parses raw text into an array of macro objects.
 * @param {string} text - Raw file / paste content.
 * @returns {Array} Array of macro objects (may be empty).
 */
function parseFileContent(text) {
  text = text.trim();
  if (!text) return [];

  /* ── Attempt 1: JSON ── */
  if (text.startsWith("[") || text.startsWith("{")) {
    try {
      var data = JSON.parse(text);
      if (!Array.isArray(data)) data = [data];
      return data
        .map(function (item) {
          return {
            id: ++nextId,
            name: item.name || item.Name || "Unnamed",
            command: item.command || item.Command || item.cmd || "",
            keybinding:
              item.keybinding ||
              item.Keybinding ||
              item.key ||
              item.shortcut ||
              "",
            category: item.category || "custom",
            description: item.description || "",
          };
        })
        .filter(function (m) {
          return m.command;
        });
    } catch (e) {
      /* not valid JSON — fall through to next format */
    }
  }

  /* ── Attempt 2: Shell aliases  alias name='command' or alias name="command" ── */
  var aliasRegex = /alias\s+([\w_-]+)\s*=\s*(['"])(.*?)\2/g;
  var aliasMatch;
  var aliases = [];
  while ((aliasMatch = aliasRegex.exec(text)) !== null) {
    aliases.push({
      id: ++nextId,
      name: aliasMatch[1].replace(/_/g, " "),
      command: aliasMatch[3],
      keybinding: "",
      category: "custom",
      description: "",
    });
  }
  if (aliases.length > 0) return aliases;

  /* ── Attempt 3: CSV — name,command,keybinding (one entry per line) ── */
  var lines = text
    .split("\n")
    .map(function (l) {
      return l.trim();
    })
    .filter(function (l) {
      return l && !l.startsWith("#");
    });
  var csvResults = [];
  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    var firstComma = line.indexOf(",");
    if (firstComma === -1) continue;
    var lastComma = line.lastIndexOf(",");

    var name, command, keybinding;
    if (firstComma === lastComma) {
      /* Only one comma — treat as name,command (no keybinding) */
      name = line.substring(0, firstComma).trim();
      command = line.substring(firstComma + 1).trim();
      keybinding = "";
    } else {
      /* Two or more commas — name,command,keybinding */
      name = line.substring(0, firstComma).trim();
      command = line.substring(firstComma + 1, lastComma).trim();
      keybinding = line.substring(lastComma + 1).trim();
    }

    if (name && command) {
      if (keybinding === "none") keybinding = "";
      csvResults.push({
        id: ++nextId,
        name: name,
        command: command,
        keybinding: keybinding,
        category: guessCategory(command),
        description: "",
      });
    }
  }
  return csvResults;
}

/**
 * guessCategory — Heuristically assigns a category to a command string.
 * Checks for common tool names to pick from: dev, file, network, system.
 * Falls back to "custom" when nothing matches.
 * @param {string} cmd - Shell
 command string.
 * @returns {string} Category identifier.
 */
function guessCategory(cmd) {
  var c = cmd.toLowerCase();
  if (
    c.includes("git") ||
    c.includes("make") ||
    c.includes("npm") ||
    c.includes("docker") ||
    c.includes("cargo")
  )
    return "dev";
  if (
    c.includes("rm ") ||
    c.includes("cp ") ||
    c.includes("mv ") ||
    c.includes("find ") ||
    c.includes("tar ") ||
    c.includes("ls")
  )
    return "file";
  if (
    c.includes("curl") ||
    c.includes("wget") ||
    c.includes("ssh") ||
    c.includes("ping") ||
    c.includes("lsof") ||
    c.includes("port")
  )
    return "network";
  if (
    c.includes("uname") ||
    c.includes("df ") ||
    c.includes("du ") ||
    c.includes("ps ") ||
    c.includes("kill") ||
    c.includes("top") ||
    c.includes("uptime")
  )
    return "system";
  return "custom";
}

/* ════════════════════════════════════════════════════════════════════════════
   ── IMPORT LOGIC
   Functions to import macros into state, show/hide the macro manager vs
   the import zone, and event listeners for all import entry-points:
   file upload, paste, drag-and-drop, "Create Manually", and
   auto-import from Poe message attachments.
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * importMacros — Merges a parsed macro array into state and shows the manager.
 * @param {Array} macros - Array of parsed macro objects to add.
 */
function importMacros(macros) {
  if (!macros.length) {
    toast("No macros found in that file", "err");
    return;
  }
  state.macros = state.macros.concat(macros);
  showManager();
  toast(
    macros.length + " macro" + (macros.length > 1 ? "s" : "") + " imported!",
  );
  renderMacros();
}

/**
 * showManager — Hides the import zone and reveals the macro manager.
 * Also (re-)renders the import-result banner with the current macro count
 * and wires up the "Clear All" button inside that banner.
 */
function showManager() {
  document.getElementById("import-zone").style.display = "none";
  document.getElementById("macro-manager").style.display = "block";
  var banner = document.getElementById("import-banner-area");
  banner.innerHTML =
    '<div class="import-banner">' +
    '<span><i class="fas fa-check-circle"></i> ' +
    state.macros.length +
    " macros loaded</span>" +
    '<button id="btn-clear-all">Clear All</button>' +
    "</div>";
  banner.querySelector("#btn-clear-all").addEventListener("click", function () {
    confirm2("Clear all macros and start over?", function () {
      state.macros = [];
      state.expSel.clear();
      document.getElementById("import-zone").style.display = "block";
      document.getElementById("macro-manager").style.display = "none";
    });
  });
}

/**
 * showImportZone — Hides the macro manager and shows the import zone again.
 * Called when the last macro is deleted.
 */
function showImportZone() {
  document.getElementById("import-zone").style.display = "block";
  document.getElementById("macro-manager").style.display = "none";
}

/* ── File upload via the hidden <input type="file"> ── */
document.getElementById("btn-upload").addEventListener("click", function () {
  document.getElementById("file-input").click();
});

document.getElementById("file-input").addEventListener("change", function (e) {
  var file = e.target.files[0];
  if (!file) return;
  var reader = new FileReader();
  reader.onload = function () {
    importMacros(parseFileContent(reader.result));
  };
  reader.readAsText(file);
  e.target.value = ""; // Reset so the same file can be re-selected
});

/* ── Paste import via the textarea + "Import from Paste" button ── */
document
  .getElementById("btn-paste-import")
  .addEventListener("click", function () {
    var text = document.getElementById("paste-area").value;
    if (!text.trim()) {
      toast("Paste some content first", "info");
      return;
    }
    importMacros(parseFileContent(text));
    document.getElementById("paste-area").value = "";
  });

/* ── Drag-and-drop onto the import zone ── */
var dropZone = document.getElementById("import-zone");
dropZone.addEventListener("dragover", function (e) {
  e.preventDefault();
  dropZone.classList.add("drag-over");
});
dropZone.addEventListener("dragleave", function () {
  dropZone.classList.remove("drag-over");
});
dropZone.addEventListener("drop", function (e) {
  e.preventDefault();
  dropZone.classList.remove("drag-over");
  var file = e.dataTransfer.files[0];
  if (!file) return;
  var reader = new FileReader();
  reader.onload = function () {
    importMacros(parseFileContent(reader.result));
  };
  reader.readAsText(file);
});

/* ── "Create Manually" button — shows an empty manager + new-macro modal ── */
document.getElementById("btn-new-empty").addEventListener("click", function () {
  showManager();
  renderMacros();
  showModal(null);
});

/* ── "Import" button inside the macro manager toolbar ── */
document
  .getElementById("btn-import-more")
  .addEventListener("click", function () {
    document.getElementById("file-input").click();
  });

/* ── Auto-import from a Poe trigger-message attachment (if available) ── */
if (typeof window.Poe !== "undefined" && window.Poe.getTriggerMessage) {
  window.Poe.getTriggerMessage()
    .then(function (msg) {
      if (msg && msg.attachments && msg.attachments.length > 0) {
        var att = msg.attachments[0];
        if (
          (att.mimeType && att.mimeType.startsWith("text")) ||
          att.name.match(/\.(csv|json|sh|txt)$/i)
        ) {
          fetch(att.url)
            .then(function (r) {
              return r.text();
            })
            .then(function (text) {
              var macros = parseFileContent(text);
              if (macros.length > 0) {
                importMacros(macros);
                toast("Auto-imported from attachment!");
              }
            })
            .catch(function () {});
        }
      }
    })
    .catch(function () {});
}

/* ════════════════════════════════════════════════════════════════════════════
   ── TAB NAVIGATION
   Switches between the Macros, AI Helper, and Export panels.
   Triggers renderExportList() + renderExportPreview() when the Export
   tab is activated so the list is always up to date.
   ════════════════════════════════════════════════════════════════════════════ */
document.querySelectorAll(".tab-btn").forEach(function (btn) {
  btn.addEventListener("click", function () {
    var t = btn.dataset.tab;
    document.querySelectorAll(".tab-btn").forEach(function (b) {
      var isActive = b.dataset.tab === t;
      b.classList.toggle("active", isActive);
      b.setAttribute("aria-selected", isActive ? "true" : "false");
    });
    document.querySelectorAll(".panel").forEach(function (p) {
      p.classList.toggle("active", p.id === "p-" + t);
    });
    if (t === "export") {
      renderExportList();
      renderExportPreview();
    }
  });
});

/* ════════════════════════════════════════════════════════════════════════════
   ── RENDER MACROS
   Builds the macro list DOM from state.macros (filtered by state.search).
   Each card includes the name, category badge, command preview,
   keybinding chip, edit/delete icon buttons, and the action bar.
   All button clicks are delegated via data-a / data-id attributes.
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * renderMacros — Re-renders the #macro-list element from current state.
 * Applies the search filter if state.search is non-empty.
 * Also keeps the import-banner macro count in sync.
 */
function renderMacros() {
  var el = document.getElementById("macro-list");
  var list = state.macros;

  /* Apply search filter */
  if (state.search) {
    var q = state.search.toLowerCase();
    list = list.filter(function (m) {
      return (
        m.name.toLowerCase().includes(q) || m.command.toLowerCase().includes(q)
      );
    });
  }

  /* Keep the banner count up to date */
  var bannerEl = document.querySelector(".import-banner span");
  if (bannerEl)
    bannerEl.innerHTML =
      '<i class="fas fa-check-circle"></i> ' +
      state.macros.length +
      " macros loaded";

  /* Empty state message */
  if (!list.length) {
    el.innerHTML =
      '<div class="empty"><i class="fas fa-terminal"></i><p>' +
      (state.macros.length
        ? "No macros match your search"
        : 'No macros yet — click "New" to create one') +
      "</p></div>";
    return;
  }

  /* Build a card for each macro */
  el.innerHTML = "";
  list.forEach(function (m, i) {
    var div = document.createElement("div");
    div.className = "macro-item";
    div.style.animationDelay = i * 0.03 + "s";
    div.innerHTML =
      '<div class="macro-item-main">' +
      '<div class="macro-top">' +
      '<span class="macro-name">' +
      esc(m.name) +
      "</span>" +
      '<span class="macro-badge">' +
      esc(m.category || "custom") +
      "</span>" +
      "</div>" +
      '<div class="macro-cmd" aria-label="Command: ' +
      esc(m.name) +
      '">' +
      esc(m.command) +
      "</div>" +
      '<div class="macro-meta">' +
      (m.keybinding
        ? '<span class="macro-key"><i class="fas fa-keyboard" aria-hidden="true" style="margin-right:4px;font-size:0.65rem;"></i>' +
          esc(m.keybinding) +
          "</span>"
        : "<span></span>") +
      '<div class="macro-edit-actions">' +
      '<button class="icon-btn" aria-label="Edit ' +
      esc(m.name) +
      '" title="Edit" data-a="edit" data-id="' +
      m.id +
      '"><i class="fas fa-pen" aria-hidden="true"></i></button>' +
      '<button class="icon-btn del" aria-label="Delete ' +
      esc(m.name) +
      '" title="Delete" data-a="del" data-id="' +
      m.id +
      '"><i class="fas fa-trash" aria-hidden="true"></i></button>' +
      "</div>" +
      "</div>" +
      "</div>" +
      '<div class="macro-actions-bar">' +
      '<button class="macro-action-btn copy-btn" data-a="copy" data-id="' +
      m.id +
      '"><i class="fas fa-copy" aria-hidden="true"></i> Copy Command</button>' +
      '<button class="macro-action-btn run-btn" data-a="howrun" data-id="' +
      m.id +
      '"><i class="fas fa-play" aria-hidden="true"></i> How to Run</button>' +
      '<button class="macro-action-btn ai-btn" data-a="explain" data-id="' +
      m.id +
      '"><i class="fas fa-brain" aria-hidden="true"></i> Explain</button>' +
      "</div>";
    el.appendChild(div);
  });

  /* Wire up all button actions via delegated data-a attribute */
  el.querySelectorAll(".icon-btn, .macro-action-btn").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var a = btn.dataset.a;
      var id = +btn.dataset.id;
      var m = state.macros.find(function (x) {
        return x.id === id;
      });
      if (!m) return;

      if (a === "copy") {
        /* Copy command to clipboard and show transient feedback */
        navigator.clipboard.writeText(m.command).then(function () {
          toast("Copied! Paste into your terminal with Ctrl+V");
          btn.classList.add("copied");
          var orig = btn.innerHTML;
          btn.innerHTML = '<i class="fas fa-check"></i> Copied!';
          setTimeout(function () {
            btn.classList.remove("copied");
            btn.innerHTML = orig;
          }, 2000);
        });
      } else if (a === "edit") {
        showModal(m);
      } else if (a === "del") {
        confirm2('Delete "' + m.name + '"?', function () {
          state.macros = state.macros.filter(function (x) {
            return x.id !== id;
          });
          renderMacros();
          if (!state.macros.length) showImportZone();
          toast("Deleted", "info");
        });
      } else if (a === "explain") {
        /* Switch to AI Helper panel in Explain mode and auto-run */
        state.aiMode = "explain";
        document.querySelectorAll(".tab-btn").forEach(function (b) {
          var isActive = b.dataset.tab === "ai";
          b.classList.toggle("active", isActive);
          b.setAttribute("aria-selected", isActive ? "true" : "false");
        });
        document.querySelectorAll(".panel").forEach(function (p) {
          p.classList.toggle("active", p.id === "p-ai");
        });
        document.querySelectorAll(".ai-toggle-btn").forEach(function (b) {
          var isActive = b.dataset.mode === "explain";
          b.classList.toggle("active", isActive);
          b.setAttribute("aria-pressed", isActive ? "true" : "false");
        });
        document.getElementById("ai-input").value = m.command;
        document.getElementById("ai-input").placeholder =
          "Paste a command to explain...";
        runAI();
      } else if (a === "howrun") {
        showRunPanel(m);
      }
    });
  });
}

/* ════════════════════════════════════════════════════════════════════════════
   ── RUN PANEL
   Modal overlay that presents three ways to execute a macro:
     1. Copy & Paste into terminal
     2. Via the C program using a keybinding
     3. As a temporary shell alias
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * showRunPanel — Builds and injects the How-to-Run modal for a given macro.
 * @param {Object} macro - The macro object to show run instructions for.
 */
function showRunPanel(macro) {
  var trigger = document.activeElement;
  var aliasName = macro.name.replace(/\s+/g, "_").toLowerCase();
  var ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML =
    '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="run-title" style="max-width:520px;">' +
    '<div class="run-panel">' +
    '<div class="run-panel-head">' +
    '<h3 id="run-title"><i class="fas fa-play-circle" aria-hidden="true"></i> Run: ' +
    esc(macro.name) +
    "</h3>" +
    '<button class="icon-btn" id="run-close" aria-label="Close"><i class="fas fa-times" aria-hidden="true"></i></button>' +
    "</div>" +
    '<div class="run-panel-body">' +
    /* Option 1 — Copy & Paste */
    '<div class="run-option">' +
    '<div class="run-option-label"><div class="opt-icon opt-copy"><i class="fas fa-copy"></i></div> Quick: Copy &amp; Paste</div>' +
    '<div class="run-option-steps">' +
    "<ol>" +
    "<li>Click the button below to copy the command</li>" +
    "<li>Open your <strong>terminal</strong> (Terminal app, VS Code terminal, etc.)</li>" +
    "<li>Paste with <strong>Ctrl+V</strong> (or <strong>Cmd+V</strong> on Mac)</li>" +
    "<li>Press <strong>Enter</strong> to run it</li>" +
    "</ol>" +
    "</div>" +
    '<div class="run-option-action">' +
    '<button class="btn btn-primary btn-sm" id="run-copy"><i class="fas fa-copy"></i> Copy Command</button>' +
    "</div>" +
    "</div>" +
    /* Option 2 — C program keybinding */
    '<div class="run-option">' +
    '<div class="run-option-label"><div class="opt-icon opt-program"><i class="fas fa-keyboard"></i></div> With C Program (Keybinding)</div>' +
    '<div class="run-option-steps">' +
    (macro.keybinding
      ? "<ol>" +
        "<li>Export as CSV from the <strong>Export</strong> tab, then download</li>" +
        "<li>Compile &amp; import: <code>make && ./macro ~/Downloads/macroforge_export.csv</code></li>" +
        "<li>Choose option <strong>[4] Run macro by keybinding</strong></li>" +
        "<li>Press <strong>" +
        esc(macro.keybinding) +
        "</strong> to execute</li>" +
        "</ol>"
      : "<ol>" +
        '<li>This macro has no keybinding yet — <a href="#" id="run-add-key" style="color:var(--accent);">add one</a></li>' +
        "<li>After adding, export as CSV from the Export tab</li>" +
        "<li>Run <code>make && ./macro ~/Downloads/macroforge_export.csv</code></li>" +
        "<li>Choose option [3] to run by name, or [4] to run by keybinding</li>" +
        "</ol>") +
    "</div>" +
    "</div>" +
    /* Option 3 — Shell alias */
    '<div class="run-option">' +
    '<div class="run-option-label"><div class="opt-icon opt-shell"><i class="fas fa-terminal"></i></div> As Shell Alias</div>' +
    '<div class="run-option-steps">' +
    "<ol>" +
    "<li>The alias for this macro is: <code>" +
    esc(aliasName) +
    "</code></li>" +
    "<li>Copy and run this in your terminal:</li>" +
    "</ol>" +
    '<div style="margin-top:6px;background:var(--code-bg);border:1px solid var(--border);border-radius:6px;padding:8px 10px;font-family:var(--font-code);font-size:0.78rem;color:var(--accent);word-break:break-all;">' +
    "alias " +
    esc(aliasName) +
    "='" +
    esc(macro.command.replace(/'/g, "'\\''")) +
    "'" +
    "</div>" +
    '<div style="margin-top:8px;">' +
    '<button class="btn btn-ghost btn-sm" id="run-copy-alias"><i class="fas fa-copy"></i> Copy Alias</button>' +
    "</div>" +
    "</div>" +
    "</div>" +
    "</div>" +
    "</div>" +
    "</div>";
  document.getElementById("modal-root").appendChild(ov);
  var removeTrap = trapFocus(ov.querySelector(".modal"));
  var closePanel = function () {
    removeTrap();
    ov.remove();
    if (trigger && trigger.focus) trigger.focus();
  };
  ov.addEventListener("click", function (e) {
    if (e.target === ov) closePanel();
  });
  ov.addEventListener("keydown", function (e) {
    if (e.key === "Escape") closePanel();
  });
  ov.querySelector("#run-close").onclick = closePanel;
  /* Copy the raw command to clipboard */
  ov.querySelector("#run-copy").addEventListener("click", function () {
    navigator.clipboard.writeText(macro.command).then(function () {
      toast("Copied! Now paste in your terminal");
    });
  });
  /* Copy the shell alias definition to clipboard */
  ov.querySelector("#run-copy-alias").addEventListener("click", function () {
    var aliasCmd =
      "alias " + aliasName + "='" + macro.command.replace(/'/g, "'\\''") + "'";
    navigator.clipboard.writeText(aliasCmd).then(function () {
      toast("Alias copied! Paste in terminal");
    });
  });
  /* "add one" link — closes this panel and opens the edit modal */
  var addKeyLink = ov.querySelector("#run-add-key");
  if (addKeyLink) {
    addKeyLink.addEventListener("click", function (e) {
      e.preventDefault();
      closePanel();
      showModal(macro);
    });
  }
}

/* ════════════════════════════════════════════════════════════════════════════
   ── SEARCH & NEW MACRO BUTTON
   Live-filter the macro list as the user types, and open the new-macro
   modal when the toolbar "New" button is clicked.
   ════════════════════════════════════════════════════════════════════════════ */
document.getElementById("search").addEventListener("input", function (e) {
  state.search = e.target.value;
  renderMacros();
});

document.getElementById("btn-new").addEventListener("click", function () {
  showModal(null);
});

/* ════════════════════════════════════════════════════════════════════════════
   ── KEYBINDING RECORDER
   Captures a keyboard shortcut when the user presses keys while the
   recorder is active. Normalises modifier+key into a string like
   "ctrl+shift+g" that matches the format expected by the C program.
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * startKeybindingRecorder — Activates recording mode on an input element.
 * Listens for the next keydown (ignoring lone modifier keys), builds the
 * keybinding string, writes it into inputEl, and cleans up.
 * Pressing Escape cancels without changing the input value.
 * @param {HTMLInputElement} inputEl - The text input to write the result into.
 * @param {HTMLButtonElement} btn    - The "Record" button (receives visual feedback).
 */
function startKeybindingRecorder(inputEl, btn) {
  if (btn.classList.contains("recording")) return;
  var origPlaceholder = inputEl.placeholder;
  inputEl.value = "";
  inputEl.placeholder = "Press your shortcut now... (Esc to cancel)";
  inputEl.classList.add("recording");
  btn.classList.add("recording");
  btn.innerHTML = '<i class="fas fa-circle"></i> Listening';

  var handler = function (e) {
    /* Ignore lone modifier key presses */
    if (["Control", "Alt", "Shift", "Meta"].includes(e.key)) return;

    e.preventDefault();
    e.stopPropagation();

    /* Escape cancels the recording without setting a value */
    if (e.key === "Escape") {
      finish("");
      return;
    }

    /* Build the keybinding string from modifiers + key */
    var parts = [];
    if (e.ctrlKey || e.metaKey) parts.push("ctrl");
    if (e.altKey) parts.push("alt");
    if (e.shiftKey) parts.push("shift");

    /* Normalise special key names to match C program format */
    var keyMap = {
      ArrowUp: "up",
      ArrowDown: "down",
      ArrowLeft: "left",
      ArrowRight: "right",
      Backspace: "backspace",
      Delete: "delete",
      Enter: "enter",
      Tab: "tab",
      " ": "space",
      Home: "home",
      End: "end",
      PageUp: "pageup",
      PageDown: "pagedown",
      Insert: "insert",
      Escape: "escape",
    };
    var key = e.key;
    if (keyMap[key]) {
      parts.push(keyMap[key]);
    } else if (key.match(/^F\d+$/i)) {
      parts.push(key.toLowerCase());
    } else {
      parts.push(key.toLowerCase());
    }

    finish(parts.join("+"));
  };

  var finish = function (value) {
    document.removeEventListener("keydown", handler, true);
    if (value) inputEl.value = value;
    inputEl.placeholder = origPlaceholder;
    inputEl.classList.remove("recording");
    btn.classList.remove("recording");
    btn.innerHTML = '<i class="fas fa-circle-dot"></i> Record';
  };

  document.addEventListener("keydown", handler, true);
  inputEl.focus();
}

/* ════════════════════════════════════════════════════════════════════════════
   ── MACRO MODAL (Create / Edit)
   Renders a modal form pre-filled with an existing macro's data (edit mode)
   or empty fields (create mode). Wires up the Record keybinding button,
   the inline AI command-generation button, and the Save/Create button.
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * showModal — Opens the create/edit macro modal.
 * @param {Object|null} macro - Existing macro to edit, or null to create new.
 */
function showModal(macro) {
  var trigger = document.activeElement;
  var isEdit = !!macro;
  var modalId = "modal-title-" + Date.now();
  var ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML =
    '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="' +
    modalId +
    '">' +
    '<div class="modal-head" id="' +
    modalId +
    '">' +
    (isEdit ? "Edit Macro" : "New Macro") +
    "</div>" +
    '<div class="modal-body">' +
    '<div class="field"><label class="field-label" for="m-name">Name</label>' +
    '<input class="field-input" id="m-name" placeholder="e.g. Deploy Script" value="' +
    esc(isEdit ? macro.name : "") +
    '"></div>' +
    '<div class="field"><label class="field-label" for="m-cmd">Command</label>' +
    '<div class="cmd-row">' +
    '<input class="field-input code" id="m-cmd" placeholder="e.g. make && ./deploy.sh" value="' +
    esc(isEdit ? macro.command : "") +
    '">' +
    '<button class="ai-inline-btn" id="m-ai" aria-label="Generate command with AI"><i class="fas fa-wand-magic-sparkles" aria-hidden="true"></i> AI</button>' +
    "</div>" +
    '<div class="field-hint" id="m-cmd-hint">Type a description and press AI to auto-generate</div></div>' +
    '<div class="field"><label class="field-label" for="m-key">Keybinding</label>' +
    '<div class="key-row">' +
    '<input class="field-input code" id="m-key" placeholder="e.g. ctrl+b, alt+g, f5" value="' +
    esc(isEdit && macro.keybinding ? macro.keybinding : "") +
    '">' +
    '<button class="rec-btn" id="m-key-rec" type="button"><i class="fas fa-circle-dot" aria-hidden="true"></i> Record</button>' +
    "</div>" +
    '<div class="field-hint" id="m-key-hint">Type a shortcut like ctrl+g — or click <strong>Record</strong> and press the keys</div></div>' +
    '<div class="field"><label class="field-label" for="m-cat">Category</label>' +
    '<select class="field-select" id="m-cat">' +
    '<option value="file"' +
    (isEdit && macro.category === "file" ? " selected" : "") +
    ">File Ops</option>" +
    '<option value="dev"' +
    (isEdit && macro.category === "dev" ? " selected" : "") +
    ">Dev</option>" +
    '<option value="system"' +
    (isEdit && macro.category === "system" ? " selected" : "") +
    ">System</option>" +
    '<option value="network"' +
    (isEdit && macro.category === "network" ? " selected" : "") +
    ">Network</option>" +
    '<option value="custom"' +
    (!isEdit || macro.category === "custom" ? " selected" : "") +
    ">Custom</option>" +
    "</select></div>" +
    "</div>" +
    '<div class="modal-foot">' +
    '<button class="btn btn-ghost" id="m-cancel">Cancel</button>' +
    '<button class="btn btn-primary" id="m-save"><i class="fas fa-check" aria-hidden="true"></i> ' +
    (isEdit ? "Save" : "Create") +
    "</button>" +
    "</div>" +
    "</div>";
  document.getElementById("modal-root").appendChild(ov);
  var removeTrap = trapFocus(ov.querySelector(".modal"));
  var closeModal = function () {
    removeTrap();
    ov.remove();
    if (trigger && trigger.focus) trigger.focus();
  };
  ov.addEventListener("click", function (e) {
    if (e.target === ov) closeModal();
  });
  ov.addEventListener("keydown", function (e) {
    if (e.key === "Escape") closeModal();
  });
  ov.querySelector("#m-cancel").onclick = closeModal;

  /* Record keybinding button */
  ov.querySelector("#m-key-rec").addEventListener("click", function () {
    startKeybindingRecorder(ov.querySelector("#m-key"), this);
  });

  /* Inline AI command generation — treats whatever is in the command
       field as a natural-language description and replaces it with the
       AI-generated command */
  ov.querySelector("#m-ai").addEventListener("click", async function () {
    var cmdEl = ov.querySelector("#m-cmd");
    var desc = cmdEl.value.trim();
    if (!desc) {
      toast("Type what you want first", "info");
      return;
    }
    cmdEl.value = "Generating...";
    cmdEl.disabled = true;
    try {
      await aiGenerateInline(
        desc,
        function (cmd) {
          cmdEl.value = cmd;
          cmdEl.disabled = false;
        },
        function () {
          cmdEl.value = desc;
          cmdEl.disabled = false;
          toast("AI failed", "err");
        },
      );
    } catch (e) {
      cmdEl.value = desc;
      cmdEl.disabled = false;
      toast("AI unavailable", "err");
    }
  });

  /* Save / Create button — validates fields and updates state */
  ov.querySelector("#m-save").addEventListener("click", function () {
    var name = ov.querySelector("#m-name").value.trim();
    var command = ov.querySelector("#m-cmd").value.trim();
    var keybinding = ov.querySelector("#m-key").value.trim();
    var category = ov.querySelector("#m-cat").value;
    if (!name) {
      toast("Enter a name", "err");
      return;
    }
    if (!command || command === "Generating...") {
      toast("Enter a command", "err");
      return;
    }
    if (isEdit) {
      Object.assign(macro, {
        name: name,
        command: command,
        keybinding: keybinding,
        category: category,
      });
      toast("Updated!");
    } else {
      state.macros.push({
        id: ++nextId,
        name: name,
        command: command,
        keybinding: keybinding,
        category: category,
        description: "",
      });
      toast("Created!");
    }
    closeModal();
    showManager();
    renderMacros();
  });
}

/* ════════════════════════════════════════════════════════════════════════════
   ── AI — POE INTEGRATION
   Registers three Poe message handlers and exposes runAI() / renderAI().

   Handlers:
     "ai-gen"    — non-streaming generate-command request (JSON response)
     "ai-stream" — streaming explain-command request (markdown response)
     "ai-inline" — non-streaming inline generate for the modal AI button

   hasPoe guards all Poe API calls so the app degrades gracefully when
   running outside of the Poe environment.
   ════════════════════════════════════════════════════════════════════════════ */
var hasPoe = typeof window.Poe !== "undefined";

/* inlineCbs — stores { ok, fail } callbacks keyed by a unique ID so the
   "ai-inline" handler can route responses back to the right caller. */
var inlineCbs = {};

if (hasPoe) {
  /* ── Handler: ai-gen (Generate Command — non-streaming JSON) ── */
  window.Poe.registerHandler("ai-gen", function (result) {
    var msg = result.responses[0];
    if (msg.status === "error") {
      state.aiLoading = false;
      state.aiResult = { error: msg.statusText || "Error" };
      renderAI();
    } else if (msg.status === "complete") {
      state.aiLoading = false;
      state.aiCount++;
      try {
        var match = msg.content.match(/\{[\s\S]*\}/);
        state.aiResult = match
          ? { type: "gen", data: JSON.parse(match[0]) }
          : { type: "gen", raw: msg.content };
      } catch (e) {
        state.aiResult = { type: "gen", raw: msg.content };
      }
      renderAI();
    }
  });

  /* ── Handler: ai-stream (Explain Command — streaming Markdown) ── */
  window.Poe.registerHandler("ai-stream", function (result) {
    var msg = result.responses[0];
    if (msg.status === "error") {
      state.aiLoading = false;
      state.aiResult = { error: msg.statusText || "Error" };
      renderAI();
    } else if (msg.status === "incomplete") {
      /* Partial streaming update — render with blinking cursor */
      state.aiResult = {
        type: "explain",
        content: msg.content,
        streaming: true,
      };
      renderAI();
    } else if (msg.status === "complete") {
      state.aiLoading = false;
      state.aiCount++;
      state.aiResult = {
        type: "explain",
        content: msg.content,
        streaming: false,
      };
      renderAI();
    }
  });

  /* ── Handler: ai-inline (modal inline generation — non-streaming) ── */
  window.Poe.registerHandler("ai-inline", function (result, ctx) {
    var msg = result.responses[0];
    var cb = ctx && ctx.cbId && inlineCbs[ctx.cbId];
    if (!cb) return;
    if (msg.status === "complete") {
      state.aiCount++;
      try {
        var match = msg.content.match(/\{[\s\S]*\}/);
        cb.ok(
          match
            ? JSON.parse(match[0]).command || msg.content.trim()
            : msg.content.trim(),
        );
      } catch (e) {
        cb.ok(msg.content.trim());
      }
      delete inlineCbs[ctx.cbId];
    } else if (msg.status === "error") {
      cb.fail();
      delete inlineCbs[ctx.cbId];
    }
  });
}

/**
 * aiGenerateInline — Sends a prompt to Poe and resolves with a shell command.
 * Used by the "AI" button inside the macro modal.
 * @param {string}   desc  - Natural-language description of the desired command.
 * @param {function} onOk  - Called with the generated command string on success.
 * @param {function} onFail - Called with no arguments on failure.
 */
async function aiGenerateInline(desc, onOk, onFail) {
  if (!hasPoe) {
    onFail();
    return;
  }
  var cbId = "c" + Date.now();
  inlineCbs[cbId] = { ok: onOk, fail: onFail };
  var prompt =
    'You are a terminal command expert. Generate a POSIX command for: "' +
    desc +
    '"\nRespond ONLY with raw JSON: {"command":"the command"}';
  try {
    await window.Poe.sendUserMessage("@Claude-Sonnet-4.5 " + prompt, {
      handler: "ai-inline",
      stream: false,
      openChat: false,
      handlerContext: { cbId: cbId },
      parameters: {},
    });
  } catch (e) {
    delete inlineCbs[cbId];
    onFail();
  }
}

/* ── AI mode toggle (Generate Command / Explain Command) ── */
document.querySelectorAll(".ai-toggle-btn").forEach(function (btn) {
  btn.addEventListener("click", function () {
    state.aiMode = btn.dataset.mode;
    document.querySelectorAll(".ai-toggle-btn").forEach(function (b) {
      var isActive = b.dataset.mode === state.aiMode;
      b.classList.toggle("active", isActive);
      b.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
    document.getElementById("ai-input").placeholder =
      state.aiMode === "generate"
        ? "Describe what you want to automate..."
        : "Paste a command to explain...";
  });
});

/**
 * runAI — Reads the AI input, builds the appropriate prompt, and fires the
 * Poe request. Switches prompt template based on state.aiMode:
 *   generate — expects a JSON response with command + metadata
 *   explain  — expects a streaming Markdown explanation
 */
function runAI() {
  var input = document.getElementById("ai-input").value.trim();
  if (!input) {
    toast("Enter something first", "info");
    return;
  }
  if (!hasPoe) {
    toast("Poe API not available here", "err");
    return;
  }
  state.aiLoading = true;
  state.aiResult = null;
  renderAI();

  /* Include existing macro names so AI can avoid duplicates */
  var existing = state.macros
    .map(function (m) {
      return m.name + ": " + m.command;
    })
    .join("\n");

  if (state.aiMode === "generate") {
    var prompt =
      'You are a POSIX terminal command expert.\nUser request: "' +
      input +
      '"\n\nExisting macros:\n' +
      existing +
      '\n\nRespond ONLY with raw JSON (no markdown):\n{"command":"the command","name":"short name","description":"what it does","category":"file|dev|system|network|custom","safety":"safe|warning|dangerous","safetyNote":"only if not safe"}';
    window.Poe.sendUserMessage("@Claude-Sonnet-4.5 " + prompt, {
      handler: "ai-gen",
      stream: false,
      openChat: false,
      parameters: {},
    }).catch(function (e) {
      state.aiLoading = false;
      state.aiResult = { error: e.message };
      renderAI();
    });
  } else {
    var prompt2 =
      "You are a terminal command expert. Explain this command in detail:\n\n`" +
      input +
      "`\n\nFormat as markdown:\n## Overview\nBrief summary\n## Breakdown\nExplain each part.\n## Safety\nRate as safe/warning/dangerous.";
    window.Poe.sendUserMessage("@Claude-Sonnet-4.5 " + prompt2, {
      handler: "ai-stream",
      stream: true,
      openChat: false,
      parameters: {},
    }).catch(function (e) {
      state.aiLoading = false;
      state.aiResult = { error: e.message };
      renderAI();
    });
  }
}

/* ── Run AI button ── */
document.getElementById("btn-ai").addEventListener("click", function () {
  runAI();
});

/**
 * renderAI — Re-renders the #ai-result element from state.aiLoading /
 * state.aiResult. Handles four states:
 *   loading         — blinking cursor + "AI is thinking..."
 *   null result     — placeholder robot icon
 *   error           — red error message
 *   gen / explain   — generated-command card or Markdown explanation
 */
function renderAI() {
  var el = document.getElementById("ai-result");

  if (state.aiLoading) {
    el.innerHTML =
      '<div class="ai-loading"><span class="cursor-blink"></span> AI is thinking...</div>';
    return;
  }
  if (!state.aiResult) {
    el.innerHTML =
      '<div class="ai-placeholder"><i class="fas fa-robot"></i>Results appear here</div>';
    return;
  }
  if (state.aiResult.error) {
    el.innerHTML =
      '<div style="padding:16px;color:var(--danger);"><i class="fas fa-exclamation-circle"></i> ' +
      esc(state.aiResult.error) +
      "</div>";
    return;
  }

  if (state.aiResult.type === "gen") {
    if (state.aiResult.data) {
      /* Structured JSON response — render the command card */
      var d = state.aiResult.data;
      var sc =
        d.safety === "safe"
          ? "safety-safe"
          : d.safety === "dangerous"
            ? "safety-dangerous"
            : "safety-warning";
      var si =
        d.safety === "safe"
          ? "fa-shield-halved"
          : d.safety === "dangerous"
            ? "fa-skull-crossbones"
            : "fa-triangle-exclamation";
      el.innerHTML =
        '<div class="ai-gen-card"><div class="ai-gen-label">Generated Command</div>' +
        '<div class="ai-gen-cmd">' +
        esc(d.command || "") +
        "</div>" +
        (d.description
          ? '<div class="ai-gen-desc">' + esc(d.description) + "</div>"
          : "") +
        (d.safety
          ? '<div class="ai-gen-safety ' +
            sc +
            '"><i class="fas ' +
            si +
            '"></i> ' +
            esc(d.safety) +
            (d.safetyNote ? " — " + esc(d.safetyNote) : "") +
            "</div>"
          : "") +
        '<div class="ai-gen-btns"><button class="btn btn-primary btn-sm" id="ai-add"><i class="fas fa-plus"></i> Add as Macro</button>' +
        '<button class="btn btn-ghost btn-sm" id="ai-cp"><i class="fas fa-copy"></i> Copy</button></div></div>';
      el.querySelector("#ai-add").addEventListener("click", function () {
        showAIAddModal(d);
      });
      el.querySelector("#ai-cp").addEventListener("click", function () {
        navigator.clipboard.writeText(d.command).then(function () {
          toast("Copied!");
        });
      });
    } else {
      /* Raw (non-JSON) response — render as Markdown */
      el.innerHTML =
        '<div class="ai-md">' + mdRender(state.aiResult.raw) + "</div>";
    }
  } else {
    /* Explain mode — streaming or complete Markdown */
    el.innerHTML =
      '<div class="ai-md">' +
      mdRender(state.aiResult.content || "") +
      (state.aiResult.streaming
        ? '<span class="cursor-blink" style="display:inline-block;vertical-align:middle;margin-left:2px;"></span>'
        : "") +
      "</div>";
  }
}

/* ════════════════════════════════════════════════════════════════════════════
   ── AI ADD MODAL
   Shown after the user clicks "Add as Macro" on an AI-generated command.
   Requires a keybinding before allowing the macro to be saved (needed for
   the C-program export workflow).
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * showAIAddModal — Opens a modal to confirm/edit an AI-generated macro.
 * Forces the user to supply a keybinding before saving.
 * @param {Object} d - AI result data object with command, name, category, description.
 */
function showAIAddModal(d) {
  var trigger = document.activeElement;
  var ov = document.createElement("div");
  ov.className = "overlay";
  ov.innerHTML =
    '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="ai-modal-title">' +
    '<div class="modal-head" id="ai-modal-title">Add AI-Generated Macro</div>' +
    '<div class="modal-body">' +
    '<div class="field"><label class="field-label" for="ai-m-name">Name</label>' +
    '<input class="field-input" id="ai-m-name" value="' +
    esc(d.name || "AI Macro") +
    '"></div>' +
    '<div class="field"><label class="field-label">Command</label>' +
    '<div class="field-input code" style="background:var(--code-bg);color:var(--accent);word-break:break-all;font-size:14px;">' +
    esc(d.command) +
    "</div></div>" +
    '<div class="field"><label class="field-label" for="ai-m-key">Keybinding <span style="color:var(--danger);font-weight:800;">*</span> <span style="color:var(--danger);font-size:0.7rem;font-weight:400;">(required for export)</span></label>' +
    '<div class="key-row">' +
    '<input class="field-input code" id="ai-m-key" placeholder="e.g. ctrl+b, alt+g, f5">' +
    '<button class="rec-btn" id="ai-m-key-rec" type="button"><i class="fas fa-circle-dot" aria-hidden="true"></i> Record</button>' +
    "</div>" +
    '<div class="field-hint">Type a shortcut like ctrl+g — or click <strong>Record</strong> and press the keys on your keyboard</div></div>' +
    '<div class="field-row">' +
    '<div class="field"><label class="field-label" for="ai-m-cat">Category</label>' +
    '<select class="field-select" id="ai-m-cat">' +
    '<option value="file"' +
    (d.category === "file" ? " selected" : "") +
    ">File Ops</option>" +
    '<option value="dev"' +
    (d.category === "dev" ? " selected" : "") +
    ">Dev</option>" +
    '<option value="system"' +
    (d.category === "system" ? " selected" : "") +
    ">System</option>" +
    '<option value="network"' +
    (d.category === "network" ? " selected" : "") +
    ">Network</option>" +
    '<option value="custom"' +
    (!d.category || d.category === "custom" ? " selected" : "") +
    ">Custom</option>" +
    "</select></div>" +
    "</div>" +
    "</div>" +
    '<div class="modal-foot">' +
    '<button class="btn btn-ghost" id="ai-m-cancel">Cancel</button>' +
    '<button class="btn btn-primary" id="ai-m-save"><i class="fas fa-plus" aria-hidden="true"></i> Add Macro</button>' +
    "</div>" +
    "</div>";
  document.getElementById("modal-root").appendChild(ov);
  var removeTrap = trapFocus(ov.querySelector(".modal"));
  var closeAIModal = function () {
    removeTrap();
    ov.remove();
    if (trigger && trigger.focus) trigger.focus();
  };
  ov.addEventListener("click", function (e) {
    if (e.target === ov) closeAIModal();
  });
  ov.addEventListener("keydown", function (e) {
    if (e.key === "Escape") closeAIModal();
  });
  ov.querySelector("#ai-m-cancel").onclick = closeAIModal;

  /* Record button for AI add modal */
  ov.querySelector("#ai-m-key-rec").addEventListener("click", function () {
    startKeybindingRecorder(ov.querySelector("#ai-m-key"), this);
  });

  /* Save — validates that a keybinding was provided, then pushes the macro */
  ov.querySelector("#ai-m-save").addEventListener("click", function () {
    var name = ov.querySelector("#ai-m-name").value.trim();
    var keybinding = ov.querySelector("#ai-m-key").value.trim();
    var category = ov.querySelector("#ai-m-cat").value;
    if (!name) {
      toast("Enter a name", "err");
      return;
    }
    if (!keybinding) {
      toast("Keybinding is required! Type a shortcut like ctrl+g", "err");
      return;
    }
    state.macros.push({
      id: ++nextId,
      name: name,
      command: d.command,
      keybinding: keybinding,
      category: category,
      description: d.description || "",
    });
    closeAIModal();
    showManager();
    renderMacros();
    toast("Macro added with keybinding: " + keybinding);
  });

  /* Auto-focus the keybinding input so the user can immediately type or record */
  setTimeout(function () {
    var keyEl = ov.querySelector("#ai-m-key");
    if (keyEl) keyEl.focus();
  }, 100);
}

/* ════════════════════════════════════════════════════════════════════════════
   ── EXPORT
   Renders the per-macro checkbox list, maintains the selected-ID Set,
   generates the live format preview (Shell / CSV / JSON), and handles
   the Copy and Download buttons.

   checkExportKeybindings() is called before any export action and warns
   the user if any selected macro is missing a keybinding (required by
   the C program).
   ════════════════════════════════════════════════════════════════════════════ */

/**
 * renderExportList — Rebuilds the #export-checks list from state.macros.
 * Restores checked state from state.expSel and wires up change listeners.
 */
function renderExportList() {
  var el = document.getElementById("export-checks");
  el.innerHTML = "";
  state.macros.forEach(function (m) {
    var row = document.createElement("label");
    row.className = "export-row";
    row.innerHTML =
      '<input type="checkbox" data-id="' +
      m.id +
      '"' +
      (state.expSel.has(m.id) ? " checked" : "") +
      ">" +
      '<span><span class="export-row-name">' +
      esc(m.name) +
      '</span> <span style="color:var(--text-3);font-size:0.78rem;">— ' +
      esc(m.command).substring(0, 40) +
      "</span></span>";
    el.appendChild(row);
    row.querySelector("input").addEventListener("change", function (e) {
      if (e.target.checked) state.expSel.add(m.id);
      else state.expSel.delete(m.id);
      /* Keep the Select All checkbox in sync */
      document.getElementById("sel-all").checked =
        state.expSel.size === state.macros.length;
      renderExportPreview();
    });
  });
}

/* ── Select All checkbox ── */
document.getElementById("sel-all").addEventListener("change", function (e) {
  if (e.target.checked)
    state.macros.forEach(function (m) {
      state.expSel.add(m.id);
    });
  else state.expSel.clear();
  renderExportList();
  renderExportPreview();
});

/* ── Format selector buttons (CSV / Shell / JSON) ── */
document.querySelectorAll(".fmt-btn").forEach(function (btn) {
  btn.addEventListener("click", function () {
    state.expFmt = btn.dataset.fmt;
    document.querySelectorAll(".fmt-btn").forEach(function (b) {
      var isActive = b.dataset.fmt === state.expFmt;
      b.classList.toggle("active", isActive);
      b.setAttribute("aria-pressed", isActive ? "true" : "false");
    });
    renderExportPreview();
  });
});

/**
 * renderExportPreview — Updates the live #export-code preview block.
 * Generates Shell (bash aliases), JSON array, or CSV output depending
 * on state.expFmt.
 */
function renderExportPreview() {
  var sel = state.macros.filter(function (m) {
    return state.expSel.has(m.id);
  });
  var code = document.getElementById("export-code");
  if (!sel.length) {
    code.textContent = "Select macros above to preview";
    return;
  }
  if (state.expFmt === "shell") {
    /* Shell: bash alias definitions */
    var o = "#!/bin/bash\n# MacroForge Export\n\n";
    sel.forEach(function (m) {
      o +=
        "# " +
        m.name +
        "\nalias " +
        m.name.replace(/\s+/g, "_").toLowerCase() +
        "='" +
        m.command.replace(/'/g, "'\\''") +
        "'\n\n";
    });
    code.textContent = o;
  } else if (state.expFmt === "json") {
    /* JSON: array of macro objects */
    code.textContent = JSON.stringify(
      sel.map(function (m) {
        return {
          name: m.name,
          command: m.command,
          keybinding: m.keybinding || "",
          category: m.category,
        };
      }),
      null,
      2,
    );
  } else {
    /* CSV: name,command,keybinding (compatible with the C program) */
    var o2 = "# name,command,keybinding\n";
    sel.forEach(function (m) {
      o2 += m.name + "," + m.command + "," + (m.keybinding || "none") + "\n";
    });
    code.textContent = o2;
  }
}

/**
 * checkExportKeybindings — Validates that all selected macros have keybindings.
 * Returns the array of selected macros on success, or null if validation fails.
 * @returns {Array|null} Selected macro objects, or null if validation failed.
 */
function checkExportKeybindings() {
  var sel = state.macros.filter(function (m) {
    return state.expSel.has(m.id);
  });
  if (!sel.length) {
    toast("Select macros first", "info");
    return null;
  }
  var noKey = sel.filter(function (m) {
    return !m.keybinding;
  });
  if (noKey.length > 0) {
    var names = noKey
      .map(function (m) {
        return '"' + m.name + '"';
      })
      .join(", ");
    toast(
      "Missing keybinding on: " +
        names +
        ". Edit these macros to add keybindings before exporting.",
      "err",
    );
    return null;
  }
  return sel;
}

/* ── Copy export output to clipboard ── */
document.getElementById("btn-exp-copy").addEventListener("click", function () {
  if (!checkExportKeybindings()) return;
  navigator.clipboard
    .writeText(document.getElementById("export-code").textContent)
    .then(function () {
      toast("Copied!");
    });
});

/* ── Download export output as a file ── */
document.getElementById("btn-exp-dl").addEventListener("click", function () {
  if (!checkExportKeybindings()) return;
  var text = document.getElementById("export-code").textContent;
  if (!text || text.startsWith("Select")) {
    toast("Select macros first", "info");
    return;
  }
  var exts = { shell: ".sh", json: ".json", csv: ".csv" };
  var blob = new Blob([text], { type: "text/plain" });
  var a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = "macroforge_export" + (exts[state.expFmt] || ".txt");
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(a.href);
  toast("Downloaded!");
});
