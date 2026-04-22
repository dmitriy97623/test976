# src/gui/main_window.py
from tkinter import ttk, Frame, Label, StringVar, IntVar, DoubleVar
from .pin_view import PinView


class MainWindow:
    def __init__(self, root, firmware_mode, dark_theme, on_firmware_change, on_theme_toggle, on_start, on_stop, on_export_log, on_show_log):
        self.root = root
        self.root.title("Симулятор Arduino Nano (4-20 мА)")
        self.root.geometry("850x800")

        # Переменные
        self.raw_value = IntVar(value=0)
        self.current_ma = DoubleVar(value=4.0)
        self.pressure = DoubleVar(value=0.0)
        self.status = StringVar(value="Остановлен")
        self.feedback_state = StringVar(value="Закрыт")
        self.firmware_mode = StringVar(value=firmware_mode)
        self.dark_theme = dark_theme

        self.on_firmware_change = on_firmware_change
        self.on_theme_toggle = on_theme_toggle
        self.on_start = on_start
        self.on_stop = on_stop

        self.setup_widgets()

    def setup_widgets(self):
        main_frame = Frame(self.root, padx=10, pady=10)
        main_frame.pack(fill="both", expand=True)

        # Заголовок
        Label(main_frame, text="Симулятор Arduino Nano (4-20 мА)", font=("Arial", 16, "bold")).pack(anchor="w", pady=(0, 10))

        # Текущие значения
        self.create_data_frame(main_frame)
        # Управление
        self.create_control_frame(main_frame)
        # Пины
        self.create_pin_frame(main_frame)
        # График
        self.create_plot_frame(main_frame)

    def create_data_frame(self, parent):
        frame = ttk.LabelFrame(parent, text="Текущие значения", padding=10)
        frame.pack(fill="x", pady=10)

        # АЦП, ток, давление...
        # (аналогично, но компактнее — можно сократить для примера)
        pass

    def create_control_frame(self, parent):
        frame = ttk.Frame(parent)
        frame.pack(fill="x", pady=10)

        ttk.Button(frame, text="Запустить", command=self.on_start).pack(side="left", padx=5)
        ttk.Button(frame, text="Стоп", command=self.on_stop).pack(side="left", padx=5)
        ttk.Button(frame, text="Экспорт лога", command=self.on_export_log).pack(side="left", padx=5)
        ttk.Checkbutton(frame, text="Тёмная тема", variable=self.dark_theme, command=lambda: self.on_theme_toggle(self.dark_theme.get())).pack(side="left", padx=20)

    def create_pin_frame(self, parent):
        frame = ttk.LabelFrame(parent, text="Контакты Arduino Nano", padding=10)
        frame.pack(fill="x", pady=10)
        self.pin_view = PinView(frame)
        self.pin_view.pack()

    def create_plot_frame(self, parent):
        frame = ttk.Frame(parent, height=200)
        frame.pack(fill="x", pady=10)
        frame.pack_propagate(False)
        # Здесь будет график (можно вынести в plot_widget.py)
        Label(frame, text="График АЦП (заглушка)").pack(expand=True)

    def set_dark_theme(self):
        self.root.configure(bg="#2e2e2e")
        for widget in self.root.winfo_children():
            self._set_dark(widget)

    def set_light_theme(self):
        self.root.configure(bg="SystemButtonFace")
        for widget in self.root.winfo_children():
            self._set_light(widget)

    def _set_dark(self, widget):
        if isinstance(widget, Label):
            widget.configure(bg="#2e2e2e", fg="white")
        for child in widget.winfo_children():
            self._set_dark(child)

    def _set_light(self, widget):
        if isinstance(widget, Label):
            widget.configure(bg="SystemButtonFace", fg="black")
        for child in widget.winfo_children():
            self._set_light(child)