let statusInterval = null;
let alertShown = false;

window.addEventListener("DOMContentLoaded", () => {
    const token = localStorage.getItem("api_token");

    if (!token) {
        // Если нет токена — сразу на авторизацию
        window.location.href = "auth.html";
        return;
    }

    // Всё ок — начинаем опрашивать сервер
    startStatusLoop();
});

function startStatusLoop() {
    updateStatus();
    updateLogs();
    statusInterval = setInterval(() => {
        updateStatus();
        updateLogs();
    }, 5000);
}

function kickToAuth() {
  if (!alertShown) {
    alertShown = true;
    alert("Неверный API Token! Повторите вход.");
    localStorage.removeItem("api_token");
    window.location.href = "auth.html";
  }
}

async function downloadModpack() {
    const token = localStorage.getItem("api_token");
    if (!token) return;

    try {
        const res = await fetch('/api/download-modpack', {
        headers: { "X-API-Token": token }
        });

        if (res.status === 401) return kickToAuth();

        if (!res.ok) throw new Error(`Ошибка: ${res.status}`);

        // Получаем имя файла из заголовков
        const contentDisposition = res.headers.get('Content-Disposition');
        const filename = contentDisposition 
        ? contentDisposition.split('filename=')[1].replace(/"/g, '')
        : 'modpack.zip';

        // Создаем blob и скачиваем
        const blob = await res.blob();
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        a.remove();

        document.getElementById("status").innerText = "Сборка модов скачивается...";
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при скачивании сборки";
        console.error(e);
    }
}

    async function sendCommand() {
    const token = localStorage.getItem("api_token");
    const cmdInput = document.getElementById("command");
    const cmd = cmdInput.value.trim();

    if (cmd == "stop") send('/api/stop');

    if (!cmd) return alert("Введите команду");

    try {
        const res = await fetch('/api/command', {
        method: 'POST',
        headers: {
            "Content-Type": "application/json",
            "X-API-Token": token
        },
        body: JSON.stringify({ command: cmd })
        });

        if (res.status === 401) {
        if (!alertShown) {
            alertShown = true;
            alert("Неверный API Token! Повторите вход.");
            localStorage.removeItem("api_token");
            window.location.href = "auth.html";
        }
        return;
        }

        if (!res.ok) throw new Error(`Ошибка: ${res.status}`);
        const data = await res.json();
        document.getElementById("status").innerText = "Ответ: " +  (data.status == "Запущен" ? "Выполнено!" : "Сервер отключен!");
        cmdInput.value = "";
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при отправке команды";
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
    } catch (e) {
        document.getElementById("status").innerText = "Ошибка при получении статуса";
        document.getElementById("server-ip").innerText = "—";
        document.getElementById("server-port").innerText = "—";
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
    logBox.scrollTop   = logBox.scrollHeight;   // автоскролл вниз
  } catch (e) {
    console.error("[LOG]", e);
    document.getElementById("log-box").textContent = "Не удалось загрузить логи";
  }
}