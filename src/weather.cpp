/**
 *
 */

/**
 * Perform a HTTPS GET request to the OpenWeatherMap API
 */
#include "weather.h"

void getWeather(GxEPD_Class *display, Preferences *preferences) {

  // Get the weather api key from the preferences
  if (strlen(preferences->getString("weather_api_key").c_str()) == 0) {
    log(LogLevel::INFO, "Weather API Key not set");
    return;
  }

  String condition = "";
  String temp_c = "";

  HTTPClient http;

  // Your Domain name with URL path or IP address with path
  String serverName = "https://api.weatherapi.com/v1/current.json";
  String parameters = "?key=" + preferences->getString("weather_api_key") + "&q=Munich,Germany" + "&aqi=no";

  // Your Domain name with URL path or IP address with path
  serverName += parameters;
  Serial.print("Requesting URL: ");
  Serial.println(serverName);

  http.begin(serverName.c_str());

  // Send HTTP GET request
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    // The payload is a JSON object, we need to get current.condition.text & current.temp_c

    // Get the json object from the response
    StaticJsonDocument<256> doc;

    // Parse the incoming JSON response
    DeserializationError error = deserializeJson(doc, http.getString());

    // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.c_str());
      return;
    }

    // Extract values
    condition = doc["current"]["condition"]["text"].as<String>();
    temp_c = doc["current"]["temp_c"].as<String>();

    // Save the current condition to the preferences
    preferences->putString("weather_c", condition);
    preferences->putString("weather_t", temp_c);

  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  // return the condition and temp_c
  displayWeather(display, condition, temp_c);
}