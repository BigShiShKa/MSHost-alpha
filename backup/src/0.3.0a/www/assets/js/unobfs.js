let statusInterval = null;
let alertShown = false;
let lastCommandTime = 0;
let isCommandProcessing = false;
const COMMAND_COOLDOWN = 1000;

window.addEventListener("DOMContentLoaded", () => {
    const token = localStorage.getItem("api_token");

    if (!token) {
        window.location.href = "auth.html";
        return;
    }

    startStatusLoop();

     // Обработчик Enter
    document.getElementById('command').addEventListener('keypress', function(e) {
    if (e.key === 'Enter') {
        e.preventDefault(); // Важно: предотвращаем стандартное поведение
        sendCommand();
    }
    });
});

function scrollLogsToBottom() {
    const logBox = document.getElementById('log-box');
    logBox.scrollTop = logBox.scrollHeight;
}

function startStatusLoop() {
    updateStatus();
    updateLogs();
    statusInterval = setInterval(() => {
        updateStatus();
        updateLogs();
    }, 2000);
}

function kickToAuth() {
  if (!alertShown) {
    alertShown = true;
    alert("Неверный API Token! Повторите вход.");
    localStorage.removeItem("api_token");
    window.location.href = "auth.html";
  }
}

function downloadModpack() {
    const token = localStorage.getItem("api_token");
    if (!token) return;

    const a = document.createElement("a");
    a.href = `/api/download-modpack?token=${encodeURIComponent(token)}`;
    a.download = ""; // пусть браузер сам решает имя
    a.style.display = "none";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);

    document.getElementById("status").innerText = "Скачиваю модпак...";
}



async function sendCommand() {
    if (isCommandProcessing) return; // Если команда уже выполняется - выходим
    
    const token = localStorage.getItem("api_token");
    const cmdInput = document.getElementById("command");
    const cmd = cmdInput.value.trim();

    if (document.getElementById("status").innerText !== "Статус: Запущен") {
        alert("Ошибка отправки команды! Сервер не запущен.");
        cmdInput.value = "";
        return;
    }

    if (!cmd) {
        alert("Введите команду");
        return;
    }

    isCommandProcessing = true; // Блокируем повторные отправки
    
    try {
        if (cmd === "stop") {
            await send('/api/stop');
        } else {
            const res = await fetch('/api/command', {
                method: 'POST',
                headers: {
                    "Content-Type": "application/json",
                    "X-API-Token": token
                },
                body: JSON.stringify({ command: cmd })
            });

            if (res.status === 401) kickToAuth();
            if (!res.ok) throw new Error(`Ошибка: ${res.status}`);
            
            const data = await res.json();
            document.getElementById("status").innerText = "Ответ: " + (data.status === "Запущен" ? "Выполнено!" : "Сервер отключен!");
        }
        
        cmdInput.value = "";
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при отправке команды";
        console.error(e);
    } finally {
        // Разблокируем через заданный интервал
        setTimeout(() => {
            isCommandProcessing = false;
        }, COMMAND_COOLDOWN);
    }
}

async function updateStatus() {
    const token = localStorage.getItem("api_token");
    if (!token) return;

    try {
        const res = await fetch('/api/status', {
        headers: { "X-API-Token": token }
        });

        if (res.status === 401) return kickToAuth();

        if (!res.ok) throw new Error(`Ошибка: ${res.status}`);
        
        const data = await res.json();
        document.getElementById("status").innerText = "Статус: " + (data.status || "Неизвестно");
        document.getElementById("server-ip").innerText = data.ip || "—";
        document.getElementById("server-port").innerText = data.port || "—";
        document.getElementById("server-version").innerText = data.version || "—";
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при получении статуса";
        document.getElementById("server-ip").innerText = "—";
        document.getElementById("server-port").innerText = "—";
        document.getElementById("server-version").innerText = data.version || "—";
    }
}

    async function send(path) {
    const token = localStorage.getItem("api_token");
    if (!token) return alert("Введите API Token");

    try {
        const res = await fetch(path, {
        method: 'POST', // <--- ВОТ ЭТО НУЖНО
        headers: { "X-API-Token": token }
        });

        if (res.status === 401) return kickToAuth();

        if (!res.ok) throw new Error(`Ошибка: ${res.status}`);
        const data = await res.json();

        if (path === '/api/exit') {
        document.getElementById("status").innerText = data.message || "Сервер выключается...";
        clearInterval(statusInterval); // Останавливаем опрос статуса
        setTimeout(() => {
            document.body.innerHTML = `
            <h1>🛑 Сервер отключён</h1>
            <p>Вы можете закрыть это окно.</p>
            `;
        }, 1000); // Подождать чуть-чуть, чтобы не рвало сразу
        return;
        }

        document.getElementById("status").innerText = "Статус: " + (data.status || "Нет сообщения");
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при выполнении команды";
    }
}

async function updateLogs() {
  const token = localStorage.getItem("api_token");
  if (!token) return;

  try {
    const res = await fetch("/api/logs", { headers: { "X-API-Token": token } });

    if (res.status === 401) return kickToAuth();   // вынес в функцию, чтобы не дублировать

    if (!res.ok) throw new Error(`HTTP ${res.status}`);

    const data   = await res.json();
    const logBox = document.getElementById("log-box");

    const text = Array.isArray(data.logs) ? data.logs.join("\n") : (data.logs || "Лог пуст");

    // textContent надёжнее, чем innerText (не лезет в CSS‑reflow)
    logBox.textContent = text;
  } catch (e) {
    console.error("[LOG]", e);
    document.getElementById("log-box").textContent = "Не удалось загрузить логи";
  }
}