(() => {
  const yearEl = document.getElementById("year");
  if (yearEl) yearEl.textContent = String(new Date().getFullYear());

  // Mobile nav
  const toggle = document.querySelector(".nav-toggle");
  const nav = document.querySelector(".nav");
  if (toggle && nav) {
    toggle.addEventListener("click", () => {
      const open = nav.classList.toggle("is-open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
    nav.querySelectorAll("a").forEach((link) => {
      link.addEventListener("click", () => {
        nav.classList.remove("is-open");
        toggle.setAttribute("aria-expanded", "false");
      });
    });
  }

  // Role tabs: Оператор / AI-агент / Моя система
  const tabsRoot = document.querySelector("[data-tabs]");
  if (tabsRoot) {
    const tabs = Array.from(tabsRoot.querySelectorAll("[data-tab]"));
    const panels = Array.from(tabsRoot.querySelectorAll("[data-panel]"));

    const activate = (key) => {
      tabs.forEach((tab) => {
        const on = tab.dataset.tab === key;
        tab.classList.toggle("is-active", on);
        tab.setAttribute("aria-selected", on ? "true" : "false");
      });
      panels.forEach((panel) => {
        const on = panel.dataset.panel === key;
        panel.classList.toggle("is-active", on);
        if (on) panel.removeAttribute("hidden");
        else panel.setAttribute("hidden", "");
      });
    };

    tabs.forEach((tab) => {
      tab.addEventListener("click", () => activate(tab.dataset.tab));
    });
  }

  // Select the topic for the contact form.
  const previewOptions = Array.from(document.querySelectorAll("[data-preview-topic]"));
  previewOptions.forEach((option) => {
    option.addEventListener("click", () => {
      previewOptions.forEach((item) => {
        const active = item === option;
        item.classList.toggle("is-active", active);
        item.setAttribute("aria-pressed", String(active));
      });
    });
  });

  const form = document.getElementById("contact-form");
  const previewButton = document.getElementById("preview-interest");
  const status = document.getElementById("form-status");
  if (form && previewButton && status) {
    let requestId = crypto.randomUUID();
    let submitted = false;
    let loadingCaptcha = false;
    const loadCaptcha = () => {
      if (loadingCaptcha) return;
      loadingCaptcha = true;
      const script = document.createElement("script");
      script.src = "https://smartcaptcha.yandexcloud.net/captcha.js";
      script.defer = true;
      script.onerror = () => {
        status.textContent = "CAPTCHA не загрузилась. Обновите страницу или напишите нам по email.";
      };
      document.head.append(script);
    };
    if ("IntersectionObserver" in window) {
      const captchaObserver = new IntersectionObserver((entries) => {
        if (entries.some((entry) => entry.isIntersecting)) {
          loadCaptcha();
          captchaObserver.disconnect();
        }
      }, { rootMargin: "400px" });
      captchaObserver.observe(form);
    } else {
      loadCaptcha();
    }
    form.addEventListener("focusin", loadCaptcha, { once: true });
    form.addEventListener("input", () => {
      if (!submitted) requestId = crypto.randomUUID();
    });
    form.addEventListener("submit", async (event) => {
      event.preventDefault();
      if (submitted || previewButton.disabled || !form.reportValidity()) return;
      const values = new FormData(form);
      const token = window.smartCaptcha?.getResponse() || values.get("smart-token");
      if (!token) {
        loadCaptcha();
        status.textContent = "Пройдите проверку CAPTCHA перед отправкой.";
        return;
      }
      previewButton.disabled = true;
      form.setAttribute("aria-busy", "true");
      status.textContent = "Отправляем обращение…";
      try {
        const response = await fetch(form.action, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            name: values.get("name"), email: values.get("email"), message: values.get("message"),
            topic: document.querySelector("[data-preview-topic].is-active")?.dataset.previewTopic,
            consent: values.get("consent") === "on", "smart-token": token, request_id: requestId,
          }),
          signal: AbortSignal.timeout(45000),
        });
        const result = await response.json();
        status.textContent = result.message || "Не удалось отправить обращение. Попробуйте позже.";
        if (response.ok && result.ok) {
          submitted = true;
          previewButton.textContent = "Обращение отправлено";
          form.querySelectorAll("input, textarea, [data-preview-topic]").forEach((field) => { field.disabled = true; });
        }
      } catch {
        status.textContent = "Не удалось получить подтверждение отправки. Проверьте почту перед повтором: обращение могло быть принято.";
      } finally {
        form.removeAttribute("aria-busy");
        if (!submitted) {
          previewButton.disabled = false;
          window.smartCaptcha?.reset();
        }
      }
    });
  }

  // Open screenshots at their original resolution without navigating away.
  const imageDialog = document.getElementById("image-dialog");
  if (imageDialog && typeof imageDialog.showModal === "function") {
    document.querySelectorAll("figure img").forEach((img) => {
      if (img.closest(".shot-mobile") && !img.complete) img.decoding = "async";
      const button = document.createElement("button");
      button.type = "button";
      button.className = "image-open";
      button.setAttribute("aria-label", `Увеличить изображение: ${img.alt}`);
      img.before(button);
      button.append(img);
      button.addEventListener("click", () => {
        const fullImage = imageDialog.querySelector("img");
        fullImage.src = img.dataset.fullSrc || img.currentSrc || img.src;
        fullImage.alt = img.alt;
        imageDialog.querySelector("p").textContent = img.closest("figure")?.querySelector("figcaption")?.textContent || img.alt;
        imageDialog.showModal();
      });
    });
    imageDialog.querySelector(".dialog-close").addEventListener("click", () => imageDialog.close());
    imageDialog.addEventListener("click", (event) => {
      if (event.target === imageDialog) imageDialog.close();
    });
  }

  // Reveal only when a section enters the viewport; static content remains visible without JS.
  if ("IntersectionObserver" in window && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    const targets = document.querySelectorAll(".section-head, .evidence-shot, .role-card, .steps li, .card");
    const observer = new IntersectionObserver((entries) => {
      entries.forEach((entry) => {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-visible");
          observer.unobserve(entry.target);
        }
      });
    }, { threshold: 0.08, rootMargin: "0px 0px -30px 0px" });
    targets.forEach((target) => {
      target.classList.add("reveal");
      observer.observe(target);
    });
    document.documentElement.classList.add("has-motion");
  }
})();
