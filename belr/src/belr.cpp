/*
 * belr.cpp
 * Copyright (C) 2017  Belledonne Communications SARL
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "belr/parser.h"

#include "belr/belr.h"

using namespace std;

using namespace belr;

// =============================================================================

TransitionMap::TransitionMap(){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i)
		mPossibleChars[i]=false;
}

bool TransitionMap::intersect(const TransitionMap* other){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		if (mPossibleChars[i] && other->mPossibleChars[i]) return true;
	}
	return false;
}

bool TransitionMap::intersect(const TransitionMap *other, TransitionMap *result){
	bool ret=false;
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		bool tmp=mPossibleChars[i] && other->mPossibleChars[i];
		result->mPossibleChars[i]=tmp;
		if (tmp) ret=true;
	}
	return ret;
}

void TransitionMap::merge(const TransitionMap* other){
	for(size_t i=0;i<sizeof(mPossibleChars)/sizeof(bool);++i){
		if (other->mPossibleChars[i]) mPossibleChars[i]=true;
	}
}

void Recognizer::setName(const std::string& name){
	static unsigned int id_base=0;
	mName=name;
	/* every recognizer that is given a name is given also a unique id*/
	mId=++id_base;
}

const string &Recognizer::getName()const{
	return mName;
}

size_t Recognizer::feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t match;

	#ifdef BELR_DEBUG
		cout << "Trying to match: "<<mName<<endl;
	#endif

	ParserLocalContext hctx;
	if (ctx) ctx->beginParse(hctx, shared_from_this());
	match=_feed(ctx, input, pos);
	if (match!=string::npos && match>0){
		#ifdef BELR_DEBUG
			if (mName.size()>0){
				string matched=input.substr(pos,match);
				cout<<"Matched recognizer '"<<mName<<"' with sequence '"<<matched<<"'."<<endl;
			}
		#endif
	}
	if (ctx) ctx->endParse(hctx, input, pos, match);

	return match;
}

bool Recognizer::getTransitionMap(TransitionMap* mask){
	bool ret=_getTransitionMap(mask);
	if (0 /*!mName.empty()*/){
		cout<<"TransitionMap after "<<mName<<endl;
		for(int i=0;i<256;++i){
			if (mask->mPossibleChars[i]) cout<<(char)i;
		}
		cout<<endl;
	}
	return ret;
}


bool Recognizer::_getTransitionMap(TransitionMap* mask){
	string input;
	input.resize(2,'\0');
	for(int i=0;i<256;++i){
		input[0]=i;
		if (feed(nullptr,input,0)==1)
			mask->mPossibleChars[i]=true;
	}
	return true;
}

void Recognizer::optimize(){
	optimize(0);
}

void Recognizer::optimize(int recursionLevel){
	if (recursionLevel!=0 && mId!=0) return; /*stop on rule except at level 0*/
	_optimize(++recursionLevel);
}


CharRecognizer::CharRecognizer(int to_recognize, bool caseSensitive) : mToRecognize(to_recognize), mCaseSensitive(caseSensitive){
	if (::tolower(to_recognize)==::toupper(to_recognize)){
		/*not a case caseSensitive character*/
		mCaseSensitive=true;/*no need to perform a case insensitive match*/
	}else if (!caseSensitive){
		mToRecognize=::tolower(mToRecognize);
	}
}

size_t CharRecognizer::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	int c = (unsigned char)input[pos];
	if (mCaseSensitive){
		return c == mToRecognize ? 1 : string::npos;
	}
	return ::tolower(c) == mToRecognize ? 1 : string::npos;
}

void CharRecognizer::_optimize(int recursionLevel){

}

shared_ptr<Selector> Selector::addRecognizer(const shared_ptr<Recognizer> &r){
	mElements.push_back(r);
	return static_pointer_cast<Selector> (shared_from_this());
}

bool Selector::_getTransitionMap(TransitionMap* mask){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		(*it)->getTransitionMap(mask);
	}
	return true;
}

size_t Selector::_feedExclusive(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		matched=(*it)->feed(ctx, input, pos);
		if (matched!=string::npos && matched>0) {
			return matched;
		}
	}
	return string::npos;
}

size_t Selector::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	if (mIsExclusive) return _feedExclusive(ctx, input, pos);

	size_t matched=0;
	size_t bestmatch=0;
	shared_ptr<HandlerContextBase> bestBranch;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		shared_ptr<HandlerContextBase> br;
		if (ctx) br=ctx->branch();
		matched=(*it)->feed(ctx, input, pos);
		if (matched!=string::npos && matched>bestmatch) {
			bestmatch=matched;
			if (bestBranch) ctx->removeBranch(bestBranch);
			bestBranch=br;
		}else{
			if (ctx)
				ctx->removeBranch(br);
		}
	}
	if (bestmatch==0) return string::npos;
	if (ctx && bestmatch!=string::npos){
		ctx->merge(bestBranch);
	}
	return bestmatch;
}

void Selector::_optimize(int recursionLevel){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		(*it)->optimize(recursionLevel);
	}
	TransitionMap *all=nullptr;
	bool intersectionFound=false;
	for (auto it=mElements.begin(); it!=mElements.end() && !intersectionFound; ++it){
		TransitionMap *cur=new TransitionMap();
		(*it)->getTransitionMap(cur);
		if (all){
			if (cur->intersect(all)){
				intersectionFound=true;
			}
			all->merge(cur);
			delete cur;
		}else all=cur;
	}
	if (all) delete all;
	if (!intersectionFound){
		//cout<<"Selector '"<<getName()<<"' is exclusive."<<endl;
		mIsExclusive=true;
	}
}

size_t ExclusiveSelector::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	return Selector::_feedExclusive(ctx, input, pos);
}


shared_ptr<Sequence> Sequence::addRecognizer(const shared_ptr<Recognizer> &element){
	mElements.push_back(element);
	return static_pointer_cast<Sequence>( shared_from_this());
}

bool Sequence::_getTransitionMap(TransitionMap* mask){
	bool isComplete=false;
	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		if ((*it)->getTransitionMap(mask)) {
			isComplete=true;
			break;
		}
	}
	return isComplete;
}


size_t Sequence::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;
	size_t total=0;

	for (auto it=mElements.begin(); it!=mElements.end(); ++it){
		matched=(*it)->feed(ctx, input, pos);
		if (matched==string::npos){
			return string::npos;
		}
		pos+=matched;
		total+=matched;
	}
	return total;
}

void Sequence::_optimize(int recursionLevel){
	for (auto it=mElements.begin(); it!=mElements.end(); ++it)
		(*it)->optimize(recursionLevel);
}


shared_ptr<Loop> Loop::setRecognizer(const shared_ptr<Recognizer> &element, int min, int max){
	mMin=min;
	mMax=max;
	mRecognizer=element;
	return static_pointer_cast<Loop>(shared_from_this());
}

size_t Loop::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	size_t matched=0;
	size_t total=0;
	int repeat;

	for(repeat=0;(mMax!=-1 ? repeat<mMax : true) && input[pos]!='\0';repeat++){
		matched=mRecognizer->feed(ctx, input, pos);
		if (matched==string::npos) break;
		total+=matched;
		pos+=matched;
	}
	if (repeat<mMin) return string::npos;
	return total;
}

bool Loop::_getTransitionMap(TransitionMap* mask){
	mRecognizer->getTransitionMap(mask);
	return mMin!=0; //we must say to upper layer that this loop recognizer is allowed to be optional by returning FALSE
}

void Loop::_optimize(int recursionLevel){
	mRecognizer->optimize(recursionLevel);
}


CharRange::CharRange(int begin, int end) : mBegin(begin), mEnd(end){
}

size_t CharRange::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	int c = (unsigned char)input[pos];
	if (c >= mBegin && c <= mEnd) return 1;
	return string::npos;
}

void CharRange::_optimize(int recursionLevel){

}


shared_ptr<CharRecognizer> Foundation::charRecognizer(int character, bool caseSensitive){
	return make_shared<CharRecognizer>(character, caseSensitive);
}

shared_ptr<Selector> Foundation::selector(bool isExclusive){
	return isExclusive ? make_shared<ExclusiveSelector>() : make_shared<Selector>();
}

shared_ptr<Sequence> Foundation::sequence(){
	return make_shared<Sequence>();
}

shared_ptr<Loop> Foundation::loop(){
	return make_shared<Loop>();
}

Literal::Literal(const string& lit) : mLiteral(tolower(lit)), mLiteralSize(mLiteral.size()) {

}

size_t Literal::_feed(const shared_ptr< ParserContextBase >& ctx, const string& input, size_t pos){
	size_t i;
	for(i=0;i<mLiteralSize;++i){
		if (::tolower(input[pos+i])!=mLiteral[i]) return string::npos;
	}
	return mLiteralSize;
}

void Literal::_optimize(int recursionLevel){

}

bool Literal::_getTransitionMap(TransitionMap* mask){
	mask->mPossibleChars[::tolower(mLiteral[0])]=true;
	mask->mPossibleChars[::toupper(mLiteral[0])]=true;
	return true;
}

shared_ptr<Recognizer> Utils::literal(const string & lt){
	return make_shared<Literal>(lt);
}

shared_ptr<Recognizer> Utils::char_range(int begin, int end){
	return make_shared<CharRange>(begin, end);
}

shared_ptr<Recognizer> RecognizerPointer::getPointed(){
	return mRecognizer;
}

size_t RecognizerPointer::_feed(const shared_ptr<ParserContextBase> &ctx, const string &input, size_t pos){
	if (mRecognizer){
		return mRecognizer->feed(ctx, input, pos);
	}else{
		cerr<<"RecognizerPointer with name '"<<mName<<"' is undefined"<<endl;
		abort();
	}
	return string::npos;
}

void RecognizerPointer::setPointed(const shared_ptr<Recognizer> &r){
	mRecognizer=r;
}

void RecognizerPointer::_optimize(int recursionLevel){
	/*do not call optimize() on the pointed value to avoid a loop.
	 * The grammar will do it for all rules anyway*/
}


Grammar::Grammar(const string& name) : mName(name){

}

Grammar::~Grammar() {
	for(auto it = mRecognizerPointers.begin(); it != mRecognizerPointers.end(); ++it) {
		shared_ptr<RecognizerPointer> pointer = dynamic_pointer_cast<RecognizerPointer>(*it);
		pointer->setPointed(nullptr);
	}
}

void Grammar::assignRule(const string &argname, const shared_ptr<Recognizer> &rule){
	string name=tolower(argname);
	rule->setName(name);
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>((*it).second);
		if (pointer){
			pointer->setPointed(rule);
		}else{
			cerr<<"Error: rule '"<<name<<"' is being redefined !"<<endl;
			abort();
		}
	}
	/*in any case the map should contain real recognizers (not just pointers) */
	mRules[name]=rule;
}

void Grammar::_extendRule(const string &argname, const shared_ptr<Recognizer> &rule){
	string name=tolower(argname);
	rule->setName("");
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		shared_ptr<Selector> sel=dynamic_pointer_cast<Selector>((*it).second);
		if (sel){
			sel->addRecognizer(rule);
		}else{
			cerr<<"Error: rule '"<<name<<"' cannot be extended because it was not defined with a Selector."<<endl;
			abort();
		}
	}else{
		cerr<<"Error: rule '"<<name<<"' cannot be extended because it is not defined."<<endl;
		abort();
	}
}

shared_ptr<Recognizer> Grammar::findRule(const string &argname){
	string name=tolower(argname);
	auto it=mRules.find(name);
	if (it!=mRules.end()){
		return (*it).second;
	}
	return nullptr;
}

shared_ptr<Recognizer> Grammar::getRule(const string &argname){
	shared_ptr<Recognizer> ret=findRule(argname);

	if (ret){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>(ret);
		if (pointer){
			if (pointer->getPointed()){/*if pointer is defined return the pointed value directly*/
				return pointer->getPointed();
			}else{
				return pointer;
			}
		}
		return ret;
	}else{/*the rule doesn't exist yet: return a pointer*/
		shared_ptr<RecognizerPointer> recognizerPointer = make_shared<RecognizerPointer>();
		ret=recognizerPointer;
		string name=tolower(argname);
		ret->setName(string("@")+name);
		mRules[name]=ret;
		mRecognizerPointers.push_back(recognizerPointer);
	}
	return ret;
}

void Grammar::include(const shared_ptr<Grammar>& grammar){
	for(auto it=grammar->mRules.begin();it!=grammar->mRules.end();++it){
		if (mRules.find((*it).first)!=mRules.end()){
			cerr<<"Rule '"<<(*it).first<<"' is being redefined while including grammar '"<<grammar->mName<<"' into '"<<mName<<"'"<<endl;
		}
		mRules[(*it).first]=(*it).second;
	}
}

bool Grammar::isComplete()const{
	bool ret=true;
	for(auto it=mRules.begin(); it!=mRules.end(); ++it){
		shared_ptr<RecognizerPointer> pointer=dynamic_pointer_cast<RecognizerPointer>((*it).second);
		if (pointer && !pointer->getPointed()){
			cerr<<"Rule '"<<(*it).first<<"' is not defined."<<endl;
			ret=false;
		}
	}
	return ret;
}

void Grammar::optimize(){
	for(auto it=mRules.begin(); it!=mRules.end(); ++it){
		(*it).second->optimize();
	}
}


int Grammar::getNumRules() const{
	return (int)mRules.size();
}


string belr::tolower(const string &str){
	string ret(str);
	transform(ret.begin(),ret.end(), ret.begin(), ::tolower);
	return ret;
}
