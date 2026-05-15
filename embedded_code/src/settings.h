#pragma once


/** Set up the settings for the program. Settings are persistently stored on-chip. */
void setupSettings();


///// Counter /////

/** Get the current, persistently increasing, counter value. */
int getCounter();
/** Set the current, persistently increasing, counter value. Return the new value. */
int setCounter(int counter);
/** Increment the current, persistently increasing, counter value. Return the new value. */
int incrementCounter();
