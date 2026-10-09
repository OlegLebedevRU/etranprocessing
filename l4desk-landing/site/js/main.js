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
      toggle.setAttribute("aria-label", open ? "Закрыть меню" : "Открыть меню");
    });
    nav.querySelectorAll("a").forEach((link) => {
      link.addEventListener("click", () => {
        nav.classList.remove("is-open");
        toggle.setAttribute("aria-expanded", "false");
        toggle.setAttribute("aria-label", "Открыть меню");
      });
    });
  }

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && nav?.classList.contains("is-open")) {
      nav.classList.remove("is-open");
      toggle.setAttribute("aria-expanded", "false");
      toggle.setAttribute("aria-label", "Открыть меню");
      toggle.focus();
    }
  });

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

  // Links retain a full-size fallback when JavaScript is unavailable.
  const imageDialog = document.getElementById("image-dialog");
  if (imageDialog && typeof imageDialog.showModal === "function") {
    let opener;
    document.querySelectorAll("[data-image-open]").forEach((link) => {
      link.addEventListener("click", (event) => {
        if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
        event.preventDefault();
        opener = link;
        const img = link.querySelector("img");
        const fullImage = imageDialog.querySelector("img");
        fullImage.src = link.href;
        fullImage.alt = img.alt;
        imageDialog.querySelector("p").textContent = link.dataset.caption || img.alt;
        imageDialog.showModal();
        document.body.style.overflow = "hidden";
      });
    });
    imageDialog.querySelector(".dialog-close").addEventListener("click", () => imageDialog.close());
    imageDialog.addEventListener("click", (event) => {
      if (event.target === imageDialog) {
        const bounds = imageDialog.getBoundingClientRect();
        if (event.clientX < bounds.left || event.clientX > bounds.right || event.clientY < bounds.top || event.clientY > bounds.bottom) imageDialog.close();
      }
    });
    imageDialog.addEventListener("close", () => {
      document.body.style.overflow = "";
      opener?.focus({ preventScroll: true });
    });
  }

  // Reveal only when a section enters the viewport; static content remains visible without JS.
  if ("IntersectionObserver" in window && !window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
    const targets = document.querySelectorAll(".section-head, .feature-card, .integration-card, .steps li");
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
