#include <algorithm>
#include <limits>
#include <string>

#include "hadoop/Pipes.hh"
#include "hadoop/TemplateFactory.hh"
#include "hadoop/StringUtils.hh"

using namespace std;

class CharCountMapper : public HadoopPipes::Mapper
{
    public:
        // constructor: does nothing
        CharCountMapper(HadoopPipes::TaskContext& context){}

        // map function: receives a line, outputs (word,"1")
        // to reducer.
        void map(HadoopPipes::MapContext& context)
        {
            //--- get line of text ---
            string line = context.getInputValue();

            //--- split it into words ---
            vector< string > words = HadoopUtils::splitString( line, " ");

            //--- emit each word tuple (word, "1" ) ---
            for(unsigned int i=0; i<words.size(); i++)
            {
                for(unsigned int j=0; j<words[i].length(); j++ )
                {
                    string OneLetter(1,words[i][j]);
                    context.emit( OneLetter, HadoopUtils::toString( 1 ));
                }

            }
        }
};

class CharCountReducer : public HadoopPipes::Reducer
{
    public:
        // constructor: does nothing
        CharCountReducer(HadoopPipes::TaskContext& context){}

        // reduce function
        void reduce( HadoopPipes::ReduceContext& context )
        {
            int count = 0;

            //--- get all tuples with the same key, and count their numbers ---
            while(context.nextValue())
            {
                count += HadoopUtils::toInt(context.getInputValue());
            }

            //--- emit (word, count) ---
            context.emit(context.getInputKey(), HadoopUtils::toString( count ));
        }
};

int main(int argc, char *argv[])
{
    return HadoopPipes::runTask(HadoopPipes::TemplateFactory<CharCountMapper,CharCountReducer>());
}
