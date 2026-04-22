# src/settings/config.py
import json
import os


class ConfigManager:
    def __init__(self, config_path="config/settings.json"):
        self.config_path = config_path
        self.default_settings = {
            "firmware_mode": "sensor_4_20mA",
            "pressure_max": 10.0,
            "open_threshold": 0.9,
            "close_threshold": 0.1,
            "hysteresis": 0.05,
            "dark_theme": False
        }
        self.settings = self.load()

    def load(self):
        if os.path.exists(self.config_path):
            with open(self.config_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        return self.default_settings.copy()

    def save(self):
        os.makedirs(os.path.dirname(self.config_path), exist_ok=True)
        with open(self.config_path, 'w', encoding='utf-8') as f:
            json.dump(self.settings, f, ensure_ascii=False, indent=4)

    def get(self, key, default=None):
        return self.settings.get(key, default)

    def set(self, key, value):
        self.settings[key] = value
        self.save()

    def get_path(self, name):
        paths = {
            "log_file": "logs/simulation_log.csv"
        }
        return paths.get(name, "")