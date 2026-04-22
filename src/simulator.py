# src/simulator.py
"""
Главный класс симулятора. Собирает GUI, логику, настройки.
"""
from tkinter import Tk
from src.gui.main_window import MainWindow
from src.settings.config import ConfigManager
from src.simulation.core import SimulationEngine
from src.utils.log_manager import LogManager


class ArduinoSimulator:
    def __init__(self, root: Tk):
        self.root = root
        self.config = ConfigManager()
        self.log_manager = LogManager(self.config.get_path("log_file"))
        self.simulation = SimulationEngine(self.config, self.log_manager)

        # Создаём интерфейс
        self.main_window = MainWindow(
            root,
            firmware_mode=self.config.get("firmware_mode"),
            dark_theme=self.config.get("dark_theme"),
            on_firmware_change=self.on_firmware_change,
            on_theme_toggle=self.on_theme_toggle,
            on_start=self.simulation.start,
            on_stop=self.simulation.stop,
            on_export_log=self.export_log,
            on_show_log=self.show_log
        )

        # Привязка обновления пинов
        self.update_pin_usage()

    def on_firmware_change(self, mode: str):
        self.config.set("firmware_mode", mode)
        self.update_pin_usage()

    def on_theme_toggle(self, is_dark: bool):
        self.config.set("dark_theme", is_dark)
        if is_dark:
            self.main_window.set_dark_theme()
        else:
            self.main_window.set_light_theme()

    def update_pin_usage(self):
        mode = self.main_window.firmware_mode.get()
        self.main_window.pin_view.update_pins(mode)

    def export_log(self):
        self.log_manager.export_log(self.root)

    def show_log(self):
        self.log_manager.show_log(self.root)

    def run(self):
        self.root.mainloop()