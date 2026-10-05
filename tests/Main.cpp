#include <juce_core/juce_core.h>

int main (int argc, char** argv)
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // optional filter: DumbleTests "<substring of test name>"
    if (argc > 1)
    {
        juce::Array<juce::UnitTest*> selected;
        for (auto* t : juce::UnitTest::getTestsInCategory ("Dumble"))
            if (t->getName().containsIgnoreCase (argv[1]))
                selected.add (t);
        runner.runTests (selected);
    }
    else
    {
        runner.runTestsInCategory ("Dumble");
    }

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
