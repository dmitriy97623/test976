"""
simulator_gui.py

Автор: GigaCode (создано для Дмитрия)
Версия: 1.2
Описание:
    Графический симулятор Arduino Nano с визуализацией пинов,
    поддержкой нескольких прошивок, цветами, tooltips,
    сохранением настроек, тёмной темой, экспорт лога.
"""

import tkinter as tk
from tkinter import ttk, simpledialog, messagebox, filedialog
import threading
import time
import random
import serial
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import subprocess
import psutil
import csv
import json
import os
from datetime import datetime


class ToolTip:
    """Простой класс для подсказок (tooltips)"""
    def __init__(self, widget, text):
        self.widget = widget
        self.text = text
        self.tooltip = None
        self.widget.bind("<Enter>", self.show_tooltip)
        self.widget.bind("<Leave>", self.hide_tooltip)

    def show_tooltip(self, event=None):
        x, y, _, _ = self.widget.bbox("insert")
        x += self.widget.winfo_rootx() + 25
        y += self.widget.winfo_rooty() + 25

        self.tooltip = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{x}+{y}")

        label = tk.Label(tw, text=self.text, justify="left",
                         background="#ffffe0", relief="solid", borderwidth=1,
                         font=("tahoma", "9", "normal"))
        label.pack()

    def hide_tooltip(self, event=None):
        if self.tooltip:
            self.tooltip.destroy()
        self.tooltip = None


class ArduinoSimulator:
    def __init__(self, root):
        self.root = root
        self.root.title("Симулятор Arduino Nano (4-20 мА)")
        self.root.geometry("850x800")
        self.root.resizable(True, True)

        # Пути
        self.config_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "config", "settings.json"))
        self.log_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "logs", "simulation_log.csv"))

        # Загрузка настроек
        self.load_settings()

        # Параметры
        self.running = False
        self.socat_process = None
        self.data_log = []
        self.max_points = 100

        # Режимы
        self.manual_mode = False
        self.valve_open = False

        # Переменные GUI
        self.raw_value = tk.IntVar(value=0)
        self.current_ma = tk.DoubleVar(value=4.0)
        self.pressure = tk.DoubleVar(value=0.0)
        self.status = tk.StringVar(value="Остановлен")
        self.valve_state = tk.StringVar(value="Закрыт")
        self.feedback_state = tk.StringVar(value="Закрыт")
        self.firmware_mode = tk.StringVar(value=self.settings.get("firmware_mode", "sensor_4_20mA"))
        self.dark_theme = tk.BooleanVar(value=self.settings.get("dark_theme", False))

        self.firmware_options = {
            "Стандартный (4–20 мА)": "sensor_4_20mA",
            "С дискретом": "sensor_with_discrete",
            "Режим калибровки": "calibration_mode"
        }

        # Используемые пины
        self.used_pins = {"A0", "D0", "D1"}
        self.pwm_pins = {"D3", "D5", "D6", "D9", "D10", "D11"}

        # Создаём директории
        self.init_directories()

        # Интерфейс
        self.setup_widgets()
        self.setup_plot()

        # Привязки
        self.firmware_mode.trace("w", lambda *args: self.update_pin_usage())
        self.dark_theme.trace("w", lambda *args: self.toggle_theme())

        # Загружаем остальные настройки
        self.apply_settings()

        # Закрытие
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    def init_directories(self):
        """Создаёт папки logs и config"""
        for folder in ["logs", "config"]:
            path = os.path.join(os.path.dirname(__file__), "..", folder)
            if not os.path.exists(path):
                os.makedirs(path, exist_ok=True)

    def load_settings(self):
        """Загружает настройки из JSON"""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r', encoding='utf-8') as f:
                    self.settings = json.load(f)
            except Exception as e:
                print(f"Ошибка загрузки настроек: {e}")
                self.settings = {}
        else:
            self.settings = {}

    def save_settings(self):
        """Сохраняет настройки в JSON"""
        data = {
            "firmware_mode": self.firmware_mode.get(),
            "pressure_max": self.pressure_max,
            "open_threshold": self.open_threshold,
            "close_threshold": self.close_threshold,
            "hysteresis": self.hysteresis,
            "dark_theme": self.dark_theme.get()
        }
        try:
            with open(self.config_file, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=4)
        except Exception as e:
            print(f"Ошибка сохранения настроек: {e}")

    def apply_settings(self):
        """Применяет загруженные настройки"""
        self.pressure_max = self.settings.get("pressure_max", 10.0)
        self.open_threshold = self.settings.get("open_threshold", 0.9)
        self.close_threshold = self.settings.get("close_threshold", 0.1)
        self.hysteresis = self.settings.get("hysteresis", 0.05)

        if self.dark_theme.get():
            self.set_dark_theme()

    def setup_widgets(self):
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky="nsew")

        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(6, weight=1)
        main_frame.rowconfigure(7, weight=1)

        # Заголовок
        self.title_label = ttk.Label(main_frame, text="Симулятор Arduino Nano (4-20 мА)", font=("Arial", 16, "bold"))
        self.title_label.grid(row=0, column=0, columnspan=7, pady=10, sticky="w")

        # Текущие значения
        frame_data = ttk.LabelFrame(main_frame, text="Текущие значения", padding="10")
        frame_data.grid(row=1, column=0, columnspan=7, pady=10, sticky="ew")
        frame_data.columnconfigure(6, weight=1)

        # АЦП
        ttk.Label(frame_data, text="АЦП (0–1023):").grid(row=0, column=0, sticky="w", pady=2)
        ttk.Label(frame_data, textvariable=self.raw_value, font=("Courier", 12)).grid(row=0, column=1, padx=10)

        # Ток
        ttk.Label(frame_data, text="Ток (мА):").grid(row=1, column=0, sticky="w", pady=2)
        ttk.Label(frame_data, textvariable=self.current_ma, font=("Courier", 12)).grid(row=1, column=1, padx=10)

        # Давление
        ttk.Label(frame_data, text="Давление:").grid(row=2, column=0, sticky="w", pady=2)
        ttk.Label(frame_data, textvariable=self.pressure, font=("Courier", 12)).grid(row=2, column=1, padx=10)

        # Ручной ввод
        ttk.Label(frame_data, text="Ручной ток (мА):").grid(row=0, column=2, sticky="w", pady=2, padx=(20, 5))
        self.current_entry = ttk.Entry(frame_data, width=8)
        self.current_entry.grid(row=0, column=3, padx=5)
        ttk.Button(frame_data, text="Установить", command=self.set_manual_current).grid(row=0, column=4, padx=5)

        # Клапан
        ttk.Label(frame_data, text="Клапан (ручное):").grid(row=1, column=2, sticky="w", pady=2, padx=(20, 5))
        ttk.Button(frame_data, text="Открыть", command=lambda: self.set_valve(True)).grid(row=1, column=3, padx=5)
        ttk.Button(frame_data, text="Закрыть", command=lambda: self.set_valve(False)).grid(row=1, column=4, padx=5)

        # Обратная связь
        ttk.Label(frame_data, text="ОС по клапану:").grid(row=2, column=2, sticky="w", pady=2, padx=(20, 5))
        self.feedback_label = ttk.Label(frame_data, textvariable=self.feedback_state, font=("Arial", 10, "bold"))
        self.feedback_label.grid(row=2, column=3, columnspan=2, sticky="w")

        # Управление
        btn_frame = ttk.Frame(main_frame)
        btn_frame.grid(row=2, column=0, columnspan=7, pady=10, sticky="w")

        ttk.Button(btn_frame, text="Настроить диапазон", command=self.set_range).grid(row=0, column=0, padx=5)
        ttk.Button(btn_frame, text="Настроить пороги", command=self.set_thresholds).grid(row=0, column=1, padx=5)
        self.start_btn = ttk.Button(btn_frame, text="Запустить", command=self.toggle_sim)
        self.start_btn.grid(row=0, column=2, padx=5)
        ttk.Button(btn_frame, text="Запустить socat", command=self.start_socat_manual).grid(row=0, column=3, padx=5)
        ttk.Button(btn_frame, text="Просмотр лога", command=self.show_log).grid(row=0, column=4, padx=5)
        ttk.Button(btn_frame, text="Экспорт лога", command=self.export_log).grid(row=0, column=5, padx=5)

        # Тема
        ttk.Checkbutton(btn_frame, text="Тёмная тема", variable=self.dark_theme).grid(row=1, column=0, sticky="w", pady=(10, 2))

        # Прошивка
        ttk.Label(btn_frame, text="Прошивка:").grid(row=1, column=1, sticky="w", pady=(10, 2))
        firmware_combo = ttk.Combobox(
            btn_frame,
            textvariable=self.firmware_mode,
            values=list(self.firmware_options.keys()),
            state="readonly",
            width=20
        )
        firmware_combo.grid(row=1, column=2, columnspan=2, sticky="w", pady=(10, 2))
        firmware_combo.current(0)

        # Статус
        ttk.Label(main_frame, textvariable=self.status, foreground="blue").grid(
            row=3, column=0, columnspan=7, pady=5, sticky="w")

        # График
        self.plot_frame = ttk.Frame(main_frame)
        self.plot_frame.grid(row=4, column=0, columnspan=7, pady=10, sticky="nsew")
        self.plot_frame.columnconfigure(0, weight=1)
        self.plot_frame.rowconfigure(0, weight=1)

        # Пины
        pin_frame = ttk.LabelFrame(main_frame, text="Контакты Arduino Nano", padding="10")
        pin_frame.grid(row=5, column=0, columnspan=7, pady=10, sticky="ew")
        pin_frame.columnconfigure(6, weight=1)

        self.pin_labels = {}

        analog_pins = ["A0", "A1", "A2", "A3", "A4", "A5"]
        digital_pins = ["D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11", "D12", "D13"]

        # Аналоговые
        ttk.Label(pin_frame, text="Аналоговые:", font=("Arial", 10, "bold")).grid(row=0, column=0, sticky="w", pady=(0, 5))
        for i, pin_name in enumerate(analog_pins):
            col = i * 2
            frame = ttk.Frame(pin_frame)
            frame.grid(row=1, column=col, columnspan=2, padx=5)
            label = tk.Label(frame, text=f"{pin_name} ●", font=("Courier", 10), fg="red")
            label.pack()
            self.pin_labels[pin_name] = label

            ToolTip(label, f"{pin_name} — аналоговый вход")

        # Цифровые
        ttk.Label(pin_frame, text="Цифровые:", font=("Arial", 10, "bold")).grid(row=2, column=0, sticky="w", pady=(10, 5))
        for i, pin_name in enumerate(digital_pins):
            col = i * 2
            frame = ttk.Frame(pin_frame)
            frame.grid(row=3, column=col, columnspan=2, padx=5)
            label = tk.Label(frame, text=f"{pin_name} ●", font=("Courier", 10), fg="red")
            label.pack()
            self.pin_labels[pin_name] = label

            if pin_name == "D0":
                ToolTip(label, "D0 (RX) — приём данных по Serial")
            elif pin_name == "D1":
                ToolTip(label, "D1 (TX) — передача данных по Serial")
            elif pin_name == "D7":
                ToolTip(label, "D7 — выход управления клапаном (реле)")
            else:
                ToolTip(label, f"{pin_name} — цифровой выход")

        # Обновляем отображение
        self.update_pin_usage()

    def update_pin_usage(self):
        mode_key = self.firmware_mode.get()
        reverse_modes = {v: k for k, v in self.firmware_options.items()}
        display_name = reverse_modes.get(mode_key, "")

        used_pins = {"A0", "D0", "D1"}
        if "С дискретом" in display_name or "калибровки" in display_name:
            used_pins.add("D7")

        for pin_name, label in self.pin_labels.items():
            if pin_name in used_pins:
                label.config(fg="green")  # 🟢 используется
            elif pin_name in self.pwm_pins:
                label.config(fg="orange")  # 🟡 PWM
            else:
                label.config(fg="red")  # 🔴 свободен

    def setup_plot(self):
        self.fig, self.ax = plt.subplots(figsize=(5, 3), dpi=100)
        self.ax.set_ylim(0, 1023)
        self.ax.set_xlim(0, self.max_points)
        self.ax.set_title("Сигнал АЦП")
        self.ax.set_ylabel("Значение")
        self.ax.grid(True)

        self.canvas = FigureCanvasTkAgg(self.fig, self.plot_frame)
        widget = self.canvas.get_tk_widget()
        widget.grid(row=0, column=0, sticky="nsew")
        self.canvas.draw_idle()

    def set_range(self):
        new_max = simpledialog.askfloat("Диапазон давления", "Макс. давление (бар):", initialvalue=self.pressure_max)
        if new_max and new_max > 0:
            self.pressure_max = new_max

    def set_thresholds(self):
        open_val = simpledialog.askfloat("Порог 'Открыт'", "Давление для открытия (бар):", initialvalue=self.open_threshold)
        close_val = simpledialog.askfloat("Порог 'Закрыт'", "Давление для закрытия (бар):", initialvalue=self.close_threshold)
        hyst_val = simpledialog.askfloat("Гистерезис", "Значение гистерезиса (бар):", initialvalue=self.hysteresis)

        if open_val is not None and close_val is not None and hyst_val is not None:
            if open_val <= close_val:
                messagebox.showwarning("Ошибка", "Порог 'открыт' должен быть больше 'закрыт'")
                return
            self.open_threshold = open_val
            self.close_threshold = close_val
            self.hysteresis = hyst_val
            self.status.set(f"Пороги обновлены: O={open_val}, C={close_val}, H={hyst_val}")

    def is_socat_running(self):
        """Проверяет, запущен ли socat"""
        for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
            try:
                if proc.info['cmdline'] and 'socat' in proc.info['cmdline']:
                    return True
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                pass
        return False

    def start_socat_manual(self):
        """Ручной запуск socat"""
        if self.is_socat_running():
            messagebox.showinfo("socat", "Уже запущен!")
            return

        try:
            cmd = ['socat', '-d', '-d', 'pty,raw,echo=0', 'pty,raw,echo=0']
            self.socat_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(1)
            if self.is_socat_running():
                self.status.set("✅ socat запущен: /dev/pts/3 ⇄ /dev/pts/4")
            else:
                self.status.set("❌ Не удалось запустить socat")
        except FileNotFoundError:
            messagebox.showerror("Ошибка", "socat не установлен.\nУстановите: sudo dnf install socat")
        except Exception as e:
            messagebox.showerror("Ошибка", f"Не удалось запустить socat:\n{e}")

    def toggle_sim(self):
        """Запуск/остановка симуляции"""
        if self.running:
            self.running = False
            self.manual_mode = False
            self.start_btn.config(text="Запустить")
            self.status.set("Остановлен")
        else:
            port = simpledialog.askstring("COM-порт", "Введите порт (например, /dev/pts/3):", initialvalue="/dev/pts/3")
            if not port:
                return
            try:
                self.ser = serial.Serial(port, 9600, timeout=1)
                time.sleep(1)
                self.running = True
                self.start_btn.config(text="Стоп")
                self.status.set(f"Передача на {port}")
                threading.Thread(target=self.run_simulation, daemon=True).start()
            except Exception as e:
                self.status.set(f"Ошибка: {e}")

    def run_simulation(self):
        """Основной цикл симуляции"""
        feedback_open = False

        while self.running:
            if self.manual_mode:
                adc_value = int((self.current_ma.get() / 20.0) * 1023)
                adc_value = max(0, min(1023, adc_value))
            else:
                adc_value = random.randint(205, 1023)

            current = (adc_value / 1023.0) * 5.0 * 4.0
            pressure = max(0.0, (current - 4.0) * (self.pressure_max / 16.0))

            if not self.manual_mode:
                self.current_ma.set(round(current, 2))
                self.pressure.set(round(pressure, 2))
                self.raw_value.set(adc_value)

            # Гистерезис
            if feedback_open:
                if pressure < self.open_threshold - self.hysteresis:
                    feedback_open = False
            else:
                if pressure > self.close_threshold + self.hysteresis:
                    feedback_open = True

            # fb_text
            if pressure < self.close_threshold:
                fb_text = "Закрыт"
                fb_color = "red"
            elif pressure > self.open_threshold:
                fb_text = "Открыт"
                fb_color = "green"
            else:
                fb_text = "Промежуток"
                fb_color = "orange"

            self.feedback_state.set(fb_text)
            self.feedback_label.config(foreground=fb_color)

            # Формат данных
            mode = self.firmware_mode.get()
            line = ""

            try:
                if mode == "sensor_4_20mA":
                    line = f"{self.raw_value.get()}\n"
                elif mode == "sensor_with_discrete":
                    valve_flag = "OPEN" if fb_text == "Открыт" else "CLOSED"
                    line = f"{self.raw_value.get()},{self.current_ma.get():.2f},{self.pressure.get():.2f},{valve_flag}\n"
                elif mode == "calibration_mode":
                    manual_flag = "MANUAL" if self.manual_mode else "AUTO"
                    line = (f"ADC:{self.raw_value.get()}|CUR:{self.current_ma.get():.2f}|PRES:{self.pressure.get():.2f}|"
                            f"VALVE:{fb_text}|MODE:{manual_flag}\n")
                else:
                    line = f"{self.raw_value.get()}\n"

                self.ser.write(line.encode())
                self.log_data(self.raw_value.get(), self.current_ma.get(), self.pressure.get())

            except Exception as e:
                print(f"Ошибка передачи: {e}")
                self.running = False
                self.status.set("Ошибка передачи")
                break

            # График
            self.data_log.append(self.raw_value.get())
            if len(self.data_log) > self.max_points:
                self.data_log.pop(0)
            self.update_plot()

            time.sleep(0.5)

    def log_data(self, adc, current, pressure):
        """Записывает данные в лог"""
        try:
            with open(self.log_file, mode='a', encoding='utf-8', newline='') as f:
                writer = csv.writer(f)
                writer.writerow([datetime.now().strftime("%H:%M:%S"), adc, round(current, 2), round(pressure, 2)])
        except Exception as e:
            print(f"Ошибка записи в лог: {e}")

    def update_plot(self):
        """Обновляет график"""
        self.ax.clear()
        self.ax.plot(range(len(self.data_log)), self.data_log, '-', lw=2)
        self.ax.set_ylim(0, 1023)
        self.ax.set_xlim(0, self.max_points)
        self.ax.set_title("АЦП — Симуляция")
        self.ax.set_ylabel("Значение")
        self.ax.grid(True)
        self.canvas.draw_idle()

    def show_log(self):
        """Показывает последние строки лога"""
        try:
            with open(self.log_file, "r", encoding="utf-8") as f:
                lines = f.readlines()[-10:]
            log_text = ''.join(lines)
            log_window = tk.Toplevel(self.root)
            log_window.title("Лог симуляции")
            log_window.geometry("600x300")
            text = tk.Text(log_window, wrap="word")
            text.insert("1.0", log_text)
            text.pack(fill="both", expand=True)
            ttk.Button(log_window, text="Обновить", command=self.show_log).pack(pady=5)
        except Exception as e:
            messagebox.showerror("Ошибка", f"Не удалось открыть лог:\n{e}")

    def export_log(self):
        """Экспорт лога в CSV"""
        file_path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
            title="Сохранить лог как..."
        )
        if not file_path:
            return
        try:
            with open(self.log_file, "r", encoding="utf-8") as src:
                lines = src.readlines()
            with open(file_path, "w", encoding="utf-8") as dst:
                dst.writelines(lines)
            messagebox.showinfo("Экспорт", f"Лог сохранён:\n{file_path}")
        except Exception as e:
            messagebox.showerror("Ошибка", f"Не удалось экспортировать:\n{e}")

    def set_manual_current(self):
        """Установка тока вручную"""
        try:
            current = float(self.current_entry.get())
            if 3.5 <= current <= 20.5:
                self.current_ma.set(round(current, 2))
                voltage = current / 4.0
                adc_value = int((voltage / 5.0) * 1023)
                self.raw_value.set(max(0, min(1023, adc_value)))
                pressure = 0.0 if current < 4.0 else (current - 4.0) * (self.pressure_max / 16.0)
                self.pressure.set(round(pressure, 2))
                self.manual_mode = True
                self.status.set(f"Ручной режим: {current} мА")
            else:
                messagebox.showwarning("Ошибка", "Ток должен быть от 3.5 до 20.5 мА")
        except ValueError:
            messagebox.showerror("Ошибка", "Введите число!")

    def set_valve(self, open_state):
        """Ручное управление клапаном"""
        self.manual_mode = True
        self.valve_open = open_state
        if open_state:
            self.valve_state.set("Открыт")
            self.feedback_label.config(foreground="green")  # ✅ было valve_label → feedback_label
            self.current_ma.set(20.0)
            self.pressure.set(self.pressure_max)
            self.raw_value.set(1023)
            self.status.set("Клапан: открыт (20 мА)")
        else:
            self.valve_state.set("Закрыт")
            self.valve_label.config(foreground="red")
            self.current_ma.set(4.0)
            self.pressure.set(0.0)
            self.raw_value.set(205)
            self.status.set("Клапан: закрыт (4 мА)")

    def on_closing(self):
        """Очистка при закрытии"""
        self.save_settings()
        self.running = False
        if hasattr(self, 'ser') and self.ser.is_open:
            self.ser.close()
        if self.socat_process:
            self.socat_process.terminate()
            try:
                self.socat_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.socat_process.kill()
            print("🛑 socat остановлен")
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = ArduinoSimulator(root)
    root.mainloop()