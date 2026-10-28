namespace CanopyStudio.ViewModels.Create;

// Makes an observable list hold the items of next, in order, by touching only
// the slots that differ. Items that stay keep their views, so the gallery and
// the form do not rebuild, and lose no focus, when a list is refreshed.
public static class CollectionSync
{
    public static void To<T>(IList<T> list, IReadOnlyList<T> next) where T : class
    {
        for (int index = 0; index < next.Count; index++)
        {
            if (index == list.Count)
            {
                list.Add(next[index]);
            }
            else if (!ReferenceEquals(list[index], next[index]))
            {
                list[index] = next[index];
            }
        }
        while (list.Count > next.Count)
        {
            list.RemoveAt(list.Count - 1);
        }
    }
}
