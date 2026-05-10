import 'dart:async';
import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:uuid/uuid.dart';
import '../models/automation.dart';
import 'device_provider.dart';

class AutomationProvider extends ChangeNotifier {
  final DeviceProvider _deviceProvider;
  final List<Automation> _automations = [];
  final _uuid = const Uuid();
  Timer? _ticker;
  final Set<String> _executedKeys = {};
  final Map<String, bool> _conditionLastMet = {};

  List<Automation> get automations => List.unmodifiable(_automations);

  AutomationProvider(this._deviceProvider) {
    _loadAutomations();
    _startTicker();
  }

  void _startTicker() {
    _ticker = Timer.periodic(const Duration(seconds: 1), (_) => _tick());
  }

  void _tick() {
    final now = DateTime.now();

    for (final auto in _automations) {
      if (!auto.enabled) continue;

      switch (auto.type) {
        case AutomationType.countdown:
          _tickCountdown(auto);
          break;
        case AutomationType.condition:
          _evaluateCondition(auto);
          break;
        case AutomationType.schedule:
        case AutomationType.sunrise:
        case AutomationType.sunset:
          _tickSchedule(auto, now);
          break;
      }
    }

    // Clean old execution keys daily at midnight
    if (now.hour == 0 && now.minute == 0 && now.second == 0) {
      _executedKeys.clear();
    }

    // Always notify to update countdown displays
    notifyListeners();
  }

  void _tickCountdown(Automation auto) {
    if (!auto.isCountdownRunning) return;
    if (auto.countdownRemaining <= 0) {
      debugPrint('[AUTO] Countdown done: ${auto.name}');
      _executeAutomation(auto);
      auto.countdownStart = null;
      _saveAutomations();
    }
  }

  void _tickSchedule(Automation auto, DateTime now) {
    int targetHour;
    int targetMinute;

    switch (auto.type) {
      case AutomationType.sunrise:
        targetHour = 6;
        targetMinute = 0;
        break;
      case AutomationType.sunset:
        targetHour = 18;
        targetMinute = 0;
        break;
      case AutomationType.schedule:
        targetHour = auto.hour ?? -1;
        targetMinute = auto.minute ?? -1;
        break;
      default:
        return;
    }

    if (targetHour < 0 || targetMinute < 0) return;

    // Unique key per automation per day+time to prevent double execution
    final key =
        '${auto.id}_${now.year}${now.month}${now.day}_${targetHour}_$targetMinute';
    if (_executedKeys.contains(key)) return;

    if (now.hour == targetHour && now.minute == targetMinute) {
      // Check day-of-week for repeating schedules
      if (auto.repeatDays.isEmpty ||
          auto.repeatDays.contains(now.weekday - 1)) {
        debugPrint(
            '[AUTO] Schedule fired: ${auto.name} at $targetHour:$targetMinute');
        _executeAutomation(auto);
        _executedKeys.add(key);

        // Disable non-repeating automations after execution
        if (auto.repeatDays.isEmpty &&
            auto.type == AutomationType.schedule) {
          auto.enabled = false;
          _saveAutomations();
        }
      }
    }
  }

  void _evaluateCondition(Automation auto) {
    if (auto.conditionDeviceId == null || auto.conditionFeature == null) return;

    final deviceList = _deviceProvider.devices
        .where((d) => d.id == auto.conditionDeviceId)
        .toList();
    if (deviceList.isEmpty) return;

    final currentValue =
        deviceList.first.featureStates[auto.conditionFeature];
    final isMet = auto.evaluateCondition(currentValue);
    final wasMet = _conditionLastMet[auto.id] ?? false;

    // Edge trigger: execute only on false → true transition
    if (isMet && !wasMet) {
      debugPrint(
          '[AUTO] Condition met: ${auto.name} (${auto.conditionText})');
      _executeAutomation(auto);
    }

    _conditionLastMet[auto.id] = isMet;
  }

  // Track last execution time per automation
  final Map<String, DateTime> _lastExecuted = {};
  DateTime? get currentTime => DateTime.now();
  DateTime? lastExecutedTime(String id) => _lastExecuted[id];

  void _executeAutomation(Automation auto) {
    debugPrint('[AUTO] Executing ${auto.actions.length} actions for: ${auto.name}');
    _lastExecuted[auto.id] = DateTime.now();
    for (final action in auto.actions) {
      debugPrint(
          '[AUTO]   -> ${action.deviceId}/${action.feature} = ${action.state}');
      _deviceProvider.sendCommand(
          action.deviceId, action.feature, action.state);
    }
  }

  void runNow(String id) {
    final auto = _automations.firstWhere((a) => a.id == id);
    _executeAutomation(auto);
    notifyListeners();
  }

  void startCountdown(String id) {
    final auto = _automations.firstWhere((a) => a.id == id);
    auto.countdownStart = DateTime.now();
    _saveAutomations();
    notifyListeners();
  }

  void stopCountdown(String id) {
    final auto = _automations.firstWhere((a) => a.id == id);
    auto.countdownStart = null;
    _saveAutomations();
    notifyListeners();
  }

  void addAutomation(Automation automation) {
    _automations.add(automation);
    _saveAutomations();
    notifyListeners();
  }

  void updateAutomation(Automation updated) {
    final index = _automations.indexWhere((a) => a.id == updated.id);
    if (index >= 0) {
      _automations[index] = updated;
      _conditionLastMet.remove(updated.id);
      _saveAutomations();
      notifyListeners();
    }
  }

  void removeAutomation(String id) {
    _automations.removeWhere((a) => a.id == id);
    _conditionLastMet.remove(id);
    _saveAutomations();
    notifyListeners();
  }

  void toggleAutomation(String id) {
    final auto = _automations.firstWhere((a) => a.id == id);
    auto.enabled = !auto.enabled;
    if (!auto.enabled) _conditionLastMet.remove(id);
    _saveAutomations();
    notifyListeners();
  }

  String generateId() => _uuid.v4();

  Future<void> _loadAutomations() async {
    final prefs = await SharedPreferences.getInstance();
    final data = prefs.getString('automations');
    if (data != null) {
      final list = jsonDecode(data) as List;
      _automations.clear();
      _automations.addAll(list.map((e) => Automation.fromJson(e)));
      notifyListeners();
    }
  }

  Future<void> _saveAutomations() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
      'automations',
      jsonEncode(_automations.map((a) => a.toJson()).toList()),
    );
  }

  @override
  void dispose() {
    _ticker?.cancel();
    super.dispose();
  }
}
