#include <juce_core/juce_core.h>

int main (int argc, char** argv)
{
    juce::ignoreUnused (argc, argv);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("Dumble");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
